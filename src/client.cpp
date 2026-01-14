#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::atomic<bool> running{true};

void receive_loop(int socket_fd) {
  char buffer[1024];
  while (running.load()) {
    std::memset(buffer, 0, sizeof(buffer));
    ssize_t received = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      std::cout << "Disconnected from server." << std::endl;
      running.store(false);
      break;
    }
    std::cout << buffer << std::flush;
  }
}

bool send_all(int socket_fd, const std::string& message) {
  const char* data = message.c_str();
  size_t total_sent = 0;
  size_t length = message.size();
  while (total_sent < length) {
    ssize_t sent = send(socket_fd, data + total_sent, length - total_sent, 0);
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

  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    std::cerr << "Socket error: " << std::strerror(errno) << "\n";
    return 1;
  }

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address: " << host << "\n";
    close(socket_fd);
    return 1;
  }

  if (connect(socket_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
    std::cerr << "Connect error: " << std::strerror(errno) << "\n";
    close(socket_fd);
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
  shutdown(socket_fd, SHUT_RDWR);
  close(socket_fd);

  if (receiver.joinable()) {
    receiver.join();
  }

  return 0;
}
