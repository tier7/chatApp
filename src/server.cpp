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
};

std::unordered_map<SocketHandle, ClientInfo> clients;
std::mutex clients_mutex;

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

std::string trim(const std::string& value) {
  size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
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
    clients[client_fd] = {client_fd, client_name};
  }

  send_system(client_fd, "Welcome! Set your name with /name <nickname>.");
  send_system(client_fd, "Use /msg <user> <message> for private chats.");

  broadcast_message("[system] " + client_name + " joined the chat.\n", client_fd);
  log_message(client_name + " joined the chat.");

  char buffer[1024];
  while (running.load()) {
    std::memset(buffer, 0, sizeof(buffer));
    SocketSize received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      break;
    }

    std::string line = trim(std::string(buffer));
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

    std::string formatted = client_name + ": " + line + "\n";
    broadcast_message(formatted);
    log_message(client_name + ": " + line);
  }

  {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients.erase(client_fd);
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
