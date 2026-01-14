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
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> running{true};

using SocketHandle =
#ifdef _WIN32
    SOCKET;
#else
    int;
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

struct RoomInfo {
  std::string name;
  bool locked;
};

struct UiState {
  std::mutex mutex;
  std::vector<std::string> room_chat;
  std::map<std::string, std::vector<std::string>> private_chats;
  std::vector<RoomInfo> rooms;
  std::vector<std::string> notifications;
  std::string current_room = "Lobby";
  std::string active_private;
};

std::string socket_error_text() {
#ifdef _WIN32
  return std::to_string(WSAGetLastError());
#else
  return std::strerror(errno);
#endif
}

int shutdown_both_flag() {
#ifdef _WIN32
  return SD_BOTH;
#else
  return SHUT_RDWR;
#endif
}

void close_socket(SocketHandle socket_fd) {
#ifdef _WIN32
  closesocket(socket_fd);
#else
  close(socket_fd);
#endif
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

std::string trim(const std::string& value) {
  size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

void append_line(std::vector<std::string>& target, const std::string& line, size_t max_lines) {
  target.push_back(line);
  if (target.size() > max_lines) {
    target.erase(target.begin(), target.begin() + (target.size() - max_lines));
  }
}

void render_ui(const UiState& state) {
  std::cout << "\033[2J\033[H";
  std::cout << "ChatApp GUI\n";
  std::cout << "Current room: " << state.current_room;
  if (!state.active_private.empty()) {
    std::cout << " | Private chat: " << state.active_private;
  }
  std::cout << "\n";
  std::cout << "Commands: /create /join /leave /rooms /msg /open /back /quit\n";
  std::cout << "------------------------------------------------------------\n";

  std::cout << "Rooms (center list)\n";
  for (const auto& room : state.rooms) {
    std::cout << "  [" << (room.locked ? "locked" : "open") << "] " << room.name << "\n";
  }

  std::cout << "------------------------------------------------------------\n";
  if (state.active_private.empty()) {
    std::cout << "Room chat\n";
    for (const auto& line : state.room_chat) {
      std::cout << "  " << line << "\n";
    }
  } else {
    std::cout << "Private chat with " << state.active_private << "\n";
    auto iter = state.private_chats.find(state.active_private);
    if (iter != state.private_chats.end()) {
      for (const auto& line : iter->second) {
        std::cout << "  " << line << "\n";
      }
    }
  }

  std::cout << "------------------------------------------------------------\n";
  std::cout << "Notifications\n";
  for (const auto& note : state.notifications) {
    std::cout << "  " << note << "\n";
  }
  std::cout << "------------------------------------------------------------\n";
  std::cout << "> " << std::flush;
}

void update_rooms(UiState& state, const std::string& payload) {
  std::vector<RoomInfo> rooms;
  std::istringstream stream(payload);
  std::string token;
  while (std::getline(stream, token, '|')) {
    if (token.empty()) {
      continue;
    }
    std::string name = token;
    if (!std::getline(stream, token, '|')) {
      break;
    }
    bool locked = token == "locked";
    rooms.push_back(RoomInfo{name, locked});
  }
  state.rooms = std::move(rooms);
}

void add_notification(UiState& state, const std::string& sender, const std::string& message) {
  std::string note = "New private message from " + sender + ": " + message;
  append_line(state.notifications, note, 5);
}

void handle_private_line(UiState& state, const std::string& line) {
  std::string payload = line.substr(std::string("[private]").size());
  payload = trim(payload);
  size_t colon = payload.find(':');
  if (colon == std::string::npos) {
    append_line(state.room_chat, line, 12);
    return;
  }
  std::string sender = trim(payload.substr(0, colon));
  std::string message = trim(payload.substr(colon + 1));
  auto& history = state.private_chats[sender];
  append_line(history, sender + ": " + message, 12);
  if (state.active_private != sender) {
    add_notification(state, sender, message);
  }
}

void handle_server_line(UiState& state, const std::string& line) {
  if (line.rfind("ROOMS|", 0) == 0) {
    update_rooms(state, line.substr(6));
    return;
  }
  if (line.rfind("ROOM|", 0) == 0) {
    state.current_room = trim(line.substr(5));
    return;
  }
  if (line.rfind("[private]", 0) == 0) {
    handle_private_line(state, line);
    return;
  }
  append_line(state.room_chat, line, 12);
}

void receive_loop(SocketHandle socket_fd, UiState& state) {
  std::string buffer_storage;
  char buffer[1024];
  while (running.load()) {
    std::memset(buffer, 0, sizeof(buffer));
    SocketSize received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      std::lock_guard<std::mutex> lock(state.mutex);
      append_line(state.room_chat, "Disconnected from server.", 12);
      running.store(false);
      render_ui(state);
      break;
    }
    buffer_storage.append(buffer, static_cast<size_t>(received));
    size_t pos = 0;
    while ((pos = buffer_storage.find('\n')) != std::string::npos) {
      std::string line = buffer_storage.substr(0, pos);
      buffer_storage.erase(0, pos + 1);
      line = trim(line);
      if (line.empty()) {
        continue;
      }
      std::lock_guard<std::mutex> lock(state.mutex);
      handle_server_line(state, line);
      render_ui(state);
    }
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int port = 5555;

  if (argc >= 2) {
    host = argv[1];
  }
  if (argc >= 3) {
    port = std::stoi(argv[2]);
  }

#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    std::cerr << "WSAStartup failed: " << socket_error_text() << "\n";
    return 1;
  }
#endif

  SocketHandle socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == kInvalidSocket) {
    std::cerr << "Socket error: " << socket_error_text() << "\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address: " << host << "\n";
    close_socket(socket_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  if (connect(socket_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
    std::cerr << "Connect error: " << socket_error_text() << "\n";
    close_socket(socket_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  UiState state;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    append_line(state.room_chat,
                "Connected. Use /create or /join to enter a room, /open <user> for private.",
                12);
    render_ui(state);
  }

  std::thread receiver(receive_loop, socket_fd, std::ref(state));

  std::string line;
  while (running.load() && std::getline(std::cin, line)) {
    line = trim(line);
    if (line.empty()) {
      render_ui(state);
      continue;
    }
    if (line == "/quit") {
      running.store(false);
      break;
    }

    if (line.rfind("/open ", 0) == 0) {
      std::string target = trim(line.substr(6));
      if (!target.empty()) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.active_private = target;
        state.notifications.erase(
            std::remove_if(state.notifications.begin(),
                           state.notifications.end(),
                           [&target](const std::string& note) {
                             return note.find("from " + target + ":") != std::string::npos;
                           }),
            state.notifications.end());
        render_ui(state);
      }
      continue;
    }

    if (line == "/back") {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.active_private.clear();
      render_ui(state);
      continue;
    }

    std::string outbound = line;
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      if (!state.active_private.empty() && line.rfind("/", 0) != 0) {
        outbound = "/msg " + state.active_private + " " + line;
        auto& history = state.private_chats[state.active_private];
        append_line(history, "you: " + line, 12);
      }
    }

    if (!send_all(socket_fd, outbound + "\n")) {
      std::cerr << "Send error.\n";
      break;
    }

    {
      std::lock_guard<std::mutex> lock(state.mutex);
      render_ui(state);
    }
  }

  running.store(false);
  shutdown(socket_fd, shutdown_both_flag());
  close_socket(socket_fd);

  if (receiver.joinable()) {
    receiver.join();
  }

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
