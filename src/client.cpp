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
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

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

void receive_loop(SocketHandle socket_fd) {
  char buffer[1024];
  while (running.load()) {
    std::memset(buffer, 0, sizeof(buffer));
    SocketSize received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      std::cout << "Disconnected from server." << std::endl;
      running.store(false);
      break;
    }
    std::cout << buffer << std::flush;
  }
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

  std::cout << "Connected to " << host << ":" << port << ". Type messages and press Enter.\n";

  std::thread receiver(receive_loop, socket_fd);

  std::string line;
  while (running.load() && std::getline(std::cin, line)) {
    if (!send_all(socket_fd, line + "\n")) {
      std::cerr << "Send error.\n";
      break;
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
