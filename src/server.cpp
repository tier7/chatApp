#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using SocketHandle =
#ifdef _WIN32
    SOCKET;
#else
    int;
#endif

using SockLenType =
#ifdef _WIN32
    int;
#else
    socklen_t;
#endif

using SocketSize =
#ifdef _WIN32
    int;
#else
    ssize_t;
#endif

constexpr SocketHandle kInvalidSocket =
#ifdef _WIN32
    INVALID_SOCKET;
#else
    -1;
#endif

struct ClientInfo {
  SocketHandle socket_fd;
  std::string name;
  std::string room;
};

struct RoomInfo {
  std::string name;
  std::string password;
  SocketHandle owner_fd;
  std::unordered_set<SocketHandle> members;
};

std::unordered_map<SocketHandle, ClientInfo> clients;
std::mutex clients_mutex;

std::unordered_map<std::string, RoomInfo> rooms;
std::mutex rooms_mutex;

std::mutex log_mutex;
std::ofstream log_file;

std::atomic<bool> running{true};

std::string socket_error_text() {
#ifdef _WIN32
  return std::to_string(WSAGetLastError());
#else
  return std::strerror(errno);
#endif
}

void close_socket(SocketHandle socket_fd) {
#ifdef _WIN32
  closesocket(socket_fd);
#else
  close(socket_fd);
#endif
}

std::string timestamp_now() {
  std::time_t now = std::time(nullptr);
  char buffer[64];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  return buffer;
}

void log_message(const std::string& message) {
  std::lock_guard<std::mutex> lock(log_mutex);
  log_file << "[" << timestamp_now() << "] " << message << '\n';
  log_file.flush();
}

bool send_all(SocketHandle socket_fd, const std::string& message) {
  const char* data = message.c_str();
  size_t total_sent = 0;
  size_t length = message.size();
  while (total_sent < length) {
    SocketSize sent =
        send(socket_fd, data + total_sent, static_cast<SocketSize>(length - total_sent), 0);
    if (sent <= 0) {
      return false;
    }
    total_sent += static_cast<size_t>(sent);
  }
  return true;
}

void broadcast_message(const std::string& message, SocketHandle exclude_fd = kInvalidSocket) {
  std::lock_guard<std::mutex> lock(clients_mutex);
  for (const auto& [fd, client] : clients) {
    if (fd == exclude_fd) {
      continue;
    }
    send_all(fd, message);
  }
}

void send_system(SocketHandle socket_fd, const std::string& message) {
  send_all(socket_fd, "[system] " + message + "\n");
}

void send_room_assignment(SocketHandle socket_fd, const std::string& room_name) {
  send_all(socket_fd, "ROOM|" + room_name + "\n");
}

std::string room_list_payload() {
  std::lock_guard<std::mutex> lock(rooms_mutex);
  std::ostringstream payload;
  payload << "ROOMS|";
  bool first = true;
  for (const auto& [name, room] : rooms) {
    if (!first) {
      payload << "|";
    }
    payload << name << "|" << (room.password.empty() ? "open" : "locked");
    first = false;
  }
  payload << "\n";
  return payload.str();
}

void send_room_list(SocketHandle socket_fd) {
  send_all(socket_fd, room_list_payload());
}

void broadcast_room_list() {
  std::string payload = room_list_payload();
  std::lock_guard<std::mutex> lock(clients_mutex);
  for (const auto& [fd, client] : clients) {
    send_all(fd, payload);
  }
}

std::string trim(const std::string& value) {
  size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

bool create_room(const std::string& room_name, const std::string& password, SocketHandle owner_fd) {
  std::lock_guard<std::mutex> lock(rooms_mutex);
  if (rooms.find(room_name) != rooms.end()) {
    return false;
  }
  rooms.emplace(room_name, RoomInfo{room_name, password, owner_fd, {}});
  return true;
}

bool join_room(SocketHandle client_fd, const std::string& room_name, const std::string& password) {
  std::lock_guard<std::mutex> lock(rooms_mutex);
  auto iter = rooms.find(room_name);
  if (iter == rooms.end()) {
    return false;
  }
  if (!iter->second.password.empty() && iter->second.password != password) {
    return false;
  }
  iter->second.members.insert(client_fd);
  return true;
}

void leave_room(SocketHandle client_fd, const std::string& room_name) {
  std::lock_guard<std::mutex> lock(rooms_mutex);
  auto iter = rooms.find(room_name);
  if (iter == rooms.end()) {
    return;
  }
  iter->second.members.erase(client_fd);
}

enum class DeleteRoomResult {
  kSuccess,
  kNotFound,
  kNotOwner,
  kLobby,
};

DeleteRoomResult delete_room(const std::string& room_name,
                             SocketHandle requester_fd,
                             std::vector<SocketHandle>* members) {
  std::lock_guard<std::mutex> lock(rooms_mutex);
  auto iter = rooms.find(room_name);
  if (iter == rooms.end()) {
    return DeleteRoomResult::kNotFound;
  }
  if (room_name == "Lobby") {
    return DeleteRoomResult::kLobby;
  }
  if (iter->second.owner_fd != requester_fd) {
    return DeleteRoomResult::kNotOwner;
  }
  members->assign(iter->second.members.begin(), iter->second.members.end());
  rooms.erase(iter);
  return DeleteRoomResult::kSuccess;
}

void broadcast_room_message(const std::string& room_name,
                            const std::string& message,
                            SocketHandle exclude_fd = kInvalidSocket) {
  std::lock_guard<std::mutex> lock(rooms_mutex);
  auto iter = rooms.find(room_name);
  if (iter == rooms.end()) {
    return;
  }
  for (SocketHandle member_fd : iter->second.members) {
    if (member_fd == exclude_fd) {
      continue;
    }
    send_all(member_fd, message);
  }
}

void handle_private_message(SocketHandle sender_fd,
                            const std::string& sender_name,
                            const std::string& command) {
  std::istringstream stream(command);
  std::string token;
  stream >> token;
  std::string target_name;
  stream >> target_name;
  std::string message;
  std::getline(stream, message);
  message = trim(message);

  if (target_name.empty() || message.empty()) {
    send_system(sender_fd, "Usage: /msg <user> <message>");
    return;
  }

  SocketHandle target_fd = kInvalidSocket;
  {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto& [fd, client] : clients) {
      if (client.name == target_name) {
        target_fd = fd;
        break;
      }
    }
  }

  if (target_fd == kInvalidSocket) {
    send_system(sender_fd, "User not found: " + target_name);
    return;
  }

  std::string formatted = "[private] " + sender_name + ": " + message + "\n";
  send_all(target_fd, formatted);
  send_all(sender_fd, formatted);
  log_message("[private] " + sender_name + " -> " + target_name + ": " + message);
}

void handle_client(SocketHandle client_fd, int client_id) {
  std::string client_name = "anon" + std::to_string(client_id);
  {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients[client_fd] = {client_fd, client_name, "Lobby"};
  }
  join_room(client_fd, "Lobby", "");
  send_room_assignment(client_fd, "Lobby");
  send_room_list(client_fd);

  send_system(client_fd, "Welcome! Set your name with /name <nickname>.");
  send_system(client_fd, "Use /msg <user> <message> for private chats.");
  send_system(client_fd,
              "Rooms: /create <room> [password], /join <room> [password], /leave, /delete <room>.");

  broadcast_room_list();

  broadcast_room_message("Lobby",
                         "[system] " + client_name + " joined the room Lobby.\n",
                         client_fd);
  log_message(client_name + " joined the room Lobby.");

  std::string incoming;
  char buffer[1024];
  while (running.load()) {
    std::memset(buffer, 0, sizeof(buffer));
    SocketSize received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      break;
    }
    incoming.append(buffer, static_cast<size_t>(received));
    size_t newline_index = incoming.find('\n');
    while (newline_index != std::string::npos) {
      std::string line = trim(incoming.substr(0, newline_index));
      incoming.erase(0, newline_index + 1);
      newline_index = incoming.find('\n');
      if (line.empty()) {
        continue;
      }

      if (line.rfind("/name ", 0) == 0) {
        std::string new_name = trim(line.substr(6));
        if (new_name.empty()) {
          send_system(client_fd, "Name cannot be empty.");
          continue;
        }
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          for (const auto& [fd, client] : clients) {
            if (client.name == new_name) {
              send_system(client_fd, "Name already in use.");
              new_name.clear();
              break;
            }
          }
          if (!new_name.empty()) {
            clients[client_fd].name = new_name;
          }
        }
        if (!new_name.empty()) {
          broadcast_message("[system] " + client_name + " is now known as " + new_name + ".\n");
          log_message(client_name + " renamed to " + new_name);
          client_name = new_name;
        }
        continue;
      }

      if (line.rfind("/msg ", 0) == 0) {
        handle_private_message(client_fd, client_name, line);
        continue;
      }

      if (line == "/rooms") {
        send_room_list(client_fd);
        continue;
      }

      if (line.rfind("/create ", 0) == 0) {
        std::istringstream stream(line.substr(8));
        std::string room_name;
        std::string password;
        stream >> room_name;
        stream >> password;
        if (room_name.empty()) {
          send_system(client_fd, "Usage: /create <room> [password]");
          continue;
        }
        if (!create_room(room_name, password, client_fd)) {
          send_system(client_fd, "Room already exists.");
          continue;
        }
        broadcast_room_list();
        std::string current_room;
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          current_room = clients[client_fd].room;
        }
        if (!join_room(client_fd, room_name, password)) {
          send_system(client_fd, "Room created, but unable to join.");
          continue;
        }
        if (!current_room.empty() && current_room != room_name) {
          leave_room(client_fd, current_room);
          broadcast_room_message(
              current_room, "[system] " + client_name + " left the room.\n", client_fd);
        }
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          clients[client_fd].room = room_name;
        }
        send_room_assignment(client_fd, room_name);
        broadcast_room_message(
            room_name, "[system] " + client_name + " joined the room.\n", client_fd);
        log_message(client_name + " joined room " + room_name);
        send_system(client_fd, "Room created and joined: " + room_name);
        continue;
      }

      if (line.rfind("/join ", 0) == 0) {
        std::istringstream stream(line.substr(6));
        std::string room_name;
        std::string password;
        stream >> room_name;
        stream >> password;
        if (room_name.empty()) {
          send_system(client_fd, "Usage: /join <room> [password]");
          continue;
        }
        std::string current_room;
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          current_room = clients[client_fd].room;
        }
        if (!join_room(client_fd, room_name, password)) {
          send_system(client_fd, "Unable to join room. Check name or password.");
          continue;
        }
        if (!current_room.empty() && current_room != room_name) {
          leave_room(client_fd, current_room);
          broadcast_room_message(
              current_room, "[system] " + client_name + " left the room.\n", client_fd);
        }
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          clients[client_fd].room = room_name;
        }
        send_room_assignment(client_fd, room_name);
        broadcast_room_message(
            room_name, "[system] " + client_name + " joined the room.\n", client_fd);
        log_message(client_name + " joined room " + room_name);
        continue;
      }

      if (line.rfind("/delete ", 0) == 0) {
        std::istringstream stream(line.substr(8));
        std::string room_name;
        stream >> room_name;
        if (room_name.empty()) {
          send_system(client_fd, "Usage: /delete <room>");
          continue;
        }
        std::vector<SocketHandle> members;
        DeleteRoomResult result = delete_room(room_name, client_fd, &members);
        if (result == DeleteRoomResult::kNotFound) {
          send_system(client_fd, "Room not found.");
          continue;
        }
        if (result == DeleteRoomResult::kLobby) {
          send_system(client_fd, "The Lobby cannot be deleted.");
          continue;
        }
        if (result == DeleteRoomResult::kNotOwner) {
          send_system(client_fd, "Only the room owner can delete it.");
          continue;
        }
        for (SocketHandle member_fd : members) {
          {
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto iter = clients.find(member_fd);
            if (iter != clients.end()) {
              iter->second.room = "Lobby";
            }
          }
          join_room(member_fd, "Lobby", "");
          send_room_assignment(member_fd, "Lobby");
          send_system(member_fd, "Room deleted. You have been moved to Lobby.");
        }
        broadcast_room_list();
        log_message(client_name + " deleted room " + room_name);
        continue;
      }

      if (line == "/leave") {
        std::string current_room;
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          current_room = clients[client_fd].room;
        }
        if (current_room.empty() || current_room == "Lobby") {
          send_system(client_fd, "You are already in the Lobby.");
          continue;
        }
        leave_room(client_fd, current_room);
        broadcast_room_message(
            current_room, "[system] " + client_name + " left the room.\n", client_fd);
        join_room(client_fd, "Lobby", "");
        {
          std::lock_guard<std::mutex> lock(clients_mutex);
          clients[client_fd].room = "Lobby";
        }
        send_room_assignment(client_fd, "Lobby");
        send_system(client_fd, "Moved to Lobby.");
        continue;
      }

      std::string current_room;
      {
        std::lock_guard<std::mutex> lock(clients_mutex);
        current_room = clients[client_fd].room;
      }

      if (current_room.empty()) {
        send_system(client_fd, "Join a room before chatting.");
        continue;
      }

      std::string formatted = "[" + current_room + "] " + client_name + ": " + line + "\n";
      broadcast_room_message(current_room, formatted);
      log_message("[" + current_room + "] " + client_name + ": " + line);
    }
  }

  std::string current_room;
  {
    std::lock_guard<std::mutex> lock(clients_mutex);
    current_room = clients[client_fd].room;
    clients.erase(client_fd);
  }
  if (!current_room.empty()) {
    leave_room(client_fd, current_room);
    broadcast_room_message(
        current_room, "[system] " + client_name + " left the room.\n", client_fd);
  }
  close_socket(client_fd);
  broadcast_message("[system] " + client_name + " left the chat.\n");
  log_message(client_name + " left the chat.");
}

void handle_signal(int) {
  running.store(false);
}
}  // namespace

int main(int argc, char* argv[]) {
  int port = 5555;
  std::string log_path = "chat.log";
  if (argc >= 2) {
    port = std::stoi(argv[1]);
  }
  if (argc >= 3) {
    log_path = argv[2];
  }

  log_file.open(log_path, std::ios::app);
  if (!log_file) {
    std::cerr << "Unable to open log file: " << log_path << "\n";
    return 1;
  }

  std::signal(SIGINT, handle_signal);

#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    std::cerr << "WSAStartup failed: " << socket_error_text() << "\n";
    return 1;
  }
#endif

  {
    std::lock_guard<std::mutex> lock(rooms_mutex);
    rooms.emplace("Lobby", RoomInfo{"Lobby", "", kInvalidSocket, {}});
  }

  SocketHandle server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == kInvalidSocket) {
    std::cerr << "Socket error: " << socket_error_text() << "\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  int opt = 1;
#ifdef _WIN32
  setsockopt(server_fd,
             SOL_SOCKET,
             SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt),
             static_cast<int>(sizeof(opt)));
#else
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    std::cerr << "Bind error: " << socket_error_text() << "\n";
    close_socket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  if (listen(server_fd, 10) < 0) {
    std::cerr << "Listen error: " << socket_error_text() << "\n";
    close_socket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  std::cout << "Chat server started on port " << port << ". Log file: " << log_path << "\n";

  int client_id = 1;
  while (running.load()) {
    sockaddr_in client_addr{};
    SockLenType client_len = sizeof(client_addr);
    SocketHandle client_fd = accept(server_fd,
                                    reinterpret_cast<sockaddr*>(&client_addr),
                                    &client_len);
    if (client_fd == kInvalidSocket) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEINTR) {
        continue;
      }
#else
      if (errno == EINTR) {
        continue;
      }
#endif
      std::cerr << "Accept error: " << socket_error_text() << "\n";
      break;
    }

    std::thread(handle_client, client_fd, client_id++).detach();
  }

  close_socket(server_fd);
  log_message("Server shutting down.");
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
