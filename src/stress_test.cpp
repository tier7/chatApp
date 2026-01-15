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
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
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

SocketHandle connect_to_host(const std::string& host, int port) {
  SocketHandle socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == kInvalidSocket) {
    std::cerr << "Socket error: " << socket_error_text() << "\n";
    return kInvalidSocket;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) <= 0) {
    std::cerr << "Invalid host address: " << host << "\n";
    close_socket(socket_fd);
    return kInvalidSocket;
  }

  if (connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    std::cerr << "Connect error: " << socket_error_text() << "\n";
    close_socket(socket_fd);
    return kInvalidSocket;
  }

  return socket_fd;
}

void handle_signal(int) {
  running.store(false);
}

void send_line(SocketHandle socket_fd, const std::string& line) {
  send_all(socket_fd, line + "\n");
}

void worker_loop(int thread_id,
                 const std::string& host,
                 int port,
                 int delay_ms,
                 std::atomic<uint64_t>& total_sent) {
  SocketHandle socket_fd = connect_to_host(host, port);
  if (socket_fd == kInvalidSocket) {
    return;
  }

  std::ostringstream name_stream;
  name_stream << "/name load_" << thread_id;
  send_line(socket_fd, name_stream.str());

  uint64_t counter = 0;
  while (running.load()) {
    std::ostringstream message_stream;
    message_stream << "load-test " << thread_id << " " << counter++;
    if (!send_all(socket_fd, message_stream.str() + "\n")) {
      break;
    }
    ++total_sent;
    if (delay_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
  }

  close_socket(socket_fd);
}
}  // namespace

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int port = 5555;
  int threads = 10;
  int delay_ms = 10;
  int duration_sec = 0;

  if (argc >= 2) {
    host = argv[1];
  }
  if (argc >= 3) {
    port = std::stoi(argv[2]);
  }
  if (argc >= 4) {
    threads = std::stoi(argv[3]);
  }
  if (argc >= 5) {
    delay_ms = std::stoi(argv[4]);
  }
  if (argc >= 6) {
    duration_sec = std::stoi(argv[5]);
  }

#ifdef _WIN32
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    std::cerr << "WSAStartup failed: " << socket_error_text() << "\n";
    return 1;
  }
#endif

  std::signal(SIGINT, handle_signal);

  std::cout << "Starting stress test with " << threads << " threads to " << host << ":" << port
            << " (delay " << delay_ms << " ms).";
  if (duration_sec > 0) {
    std::cout << " Duration: " << duration_sec << "s.";
  }
  std::cout << " Press Ctrl+C to stop.\n";

  std::atomic<uint64_t> total_sent{0};
  std::vector<std::thread> workers;
  workers.reserve(static_cast<size_t>(threads));

  auto start_time = std::chrono::steady_clock::now();
  for (int i = 0; i < threads; ++i) {
    workers.emplace_back(worker_loop, i + 1, host, port, delay_ms, std::ref(total_sent));
  }

  while (running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (duration_sec > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - start_time);
      if (elapsed.count() >= duration_sec) {
        running.store(false);
      }
    }
  }

  for (auto& worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  std::cout << "Total messages sent: " << total_sent.load() << "\n";

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
