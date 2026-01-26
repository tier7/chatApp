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
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
using UchwytGniazda =
#ifdef _WIN32
    SOCKET;
#else
    int;
#endif

using RozmiarGniazda =
#ifdef _WIN32
    int;
#else
    ssize_t;
#endif

constexpr UchwytGniazda kNieprawidloweGniazdo =
#ifdef _WIN32
    INVALID_SOCKET;
#else
    -1;
#endif

std::atomic<bool> uruchomione{true};

std::string tekst_bledu_gniazda() {
#ifdef _WIN32
  return std::to_string(WSAGetLastError());
#else
  return std::strerror(errno);
#endif
}

void zamknij_gniazdo(UchwytGniazda gniazdo) {
#ifdef _WIN32
  closesocket(gniazdo);
#else
  close(gniazdo);
#endif
}

bool wyslij_wszystko(UchwytGniazda gniazdo, const std::string& wiadomosc) {
  const char* dane = wiadomosc.c_str();
  size_t lacznie_wyslano = 0;
  size_t dlugosc = wiadomosc.size();
  while (lacznie_wyslano < dlugosc) {
    RozmiarGniazda wyslano =
        send(gniazdo, dane + lacznie_wyslano,
             static_cast<RozmiarGniazda>(dlugosc - lacznie_wyslano), 0);
    if (wyslano <= 0) {
      return false;
    }
    lacznie_wyslano += static_cast<size_t>(wyslano);
  }
  return true;
}

UchwytGniazda polacz_z_hostem(const std::string& host, int port) {
  UchwytGniazda gniazdo = socket(AF_INET, SOCK_STREAM, 0);
  if (gniazdo == kNieprawidloweGniazdo) {
    std::cerr << "Socket error: " << tekst_bledu_gniazda() << "\n";
    return kNieprawidloweGniazdo;
  }

  sockaddr_in adres{};
  adres.sin_family = AF_INET;
  adres.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &adres.sin_addr) <= 0) {
    std::cerr << "Invalid host address: " << host << "\n";
    zamknij_gniazdo(gniazdo);
    return kNieprawidloweGniazdo;
  }

  if (connect(gniazdo, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) < 0) {
    std::cerr << "Connect error: " << tekst_bledu_gniazda() << "\n";
    zamknij_gniazdo(gniazdo);
    return kNieprawidloweGniazdo;
  }

  return gniazdo;
}

void obsluz_sygnal(int) {
  uruchomione.store(false);
}

void wyslij_linie(UchwytGniazda gniazdo, const std::string& linia) {
  wyslij_wszystko(gniazdo, linia + "\n");
}

class BarieraSynchronizacji {
 public:
  explicit BarieraSynchronizacji(int uczestnicy) : uczestnicy_(uczestnicy) {}

  bool dojdz_i_czekaj() {
    std::unique_lock<std::mutex> blokada(mutex_);
    if (!aktywna_) {
      return false;
    }
    int obecna_generacja = generacja_;
    if (++doszlo_ >= uczestnicy_) {
      doszlo_ = 0;
      ++generacja_;
      warunek_.notify_all();
      return aktywna_;
    }
    warunek_.wait(blokada, [&]() { return generacja_ != obecna_generacja || !aktywna_; });
    return aktywna_;
  }

  void zatrzymaj() {
    std::lock_guard<std::mutex> blokada(mutex_);
    aktywna_ = false;
    warunek_.notify_all();
  }

 private:
  int uczestnicy_;
  int doszlo_ = 0;
  int generacja_ = 0;
  bool aktywna_ = true;
  std::mutex mutex_;
  std::condition_variable warunek_;
};

void petla_pracownika(int id_watku,
                     const std::string& host,
                     int port,
                     int opoznienie_ms,
                     BarieraSynchronizacji* bariera,
                     std::atomic<uint64_t>& lacznie_wyslano) {
  UchwytGniazda gniazdo = polacz_z_hostem(host, port);
  if (gniazdo == kNieprawidloweGniazdo) {
    return;
  }

  std::ostringstream strumien_nazwy;
  strumien_nazwy << "/name load_" << id_watku;
  wyslij_linie(gniazdo, strumien_nazwy.str());

  uint64_t licznik = 0;
  while (uruchomione.load()) {
    if (bariera != nullptr) {
      if (!bariera->dojdz_i_czekaj()) {
        break;
      }
    }
    std::ostringstream strumien_wiadomosci;
    strumien_wiadomosci << "load-test " << id_watku << " " << licznik++;
    if (!wyslij_wszystko(gniazdo, strumien_wiadomosci.str() + "\n")) {
      break;
    }
    ++lacznie_wyslano;
    if (opoznienie_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(opoznienie_ms));
    }
  }

  zamknij_gniazdo(gniazdo);
}
}  // namespace

int main(int liczba_argumentow, char* argumenty[]) {
  std::string host = "127.0.0.1";
  int port = 5555;
  int watki = 10;
  int opoznienie_ms = 10;
  int czas_trwania_s = 0;
  bool tryb_sync = false;

  if (liczba_argumentow >= 2) {
    host = argumenty[1];
  }
  if (liczba_argumentow >= 3) {
    port = std::stoi(argumenty[2]);
  }
  if (liczba_argumentow >= 4) {
    watki = std::stoi(argumenty[3]);
  }
  if (liczba_argumentow >= 5) {
    opoznienie_ms = std::stoi(argumenty[4]);
  }
  if (liczba_argumentow >= 6) {
    czas_trwania_s = std::stoi(argumenty[5]);
  }
  for (int i = 6; i < liczba_argumentow; ++i) {
    if (std::string(argumenty[i]) == "--sync") {
      tryb_sync = true;
    }
  }

#ifdef _WIN32
  WSADATA dane_wsa;
  if (WSAStartup(MAKEWORD(2, 2), &dane_wsa) != 0) {
    std::cerr << "WSAStartup failed: " << tekst_bledu_gniazda() << "\n";
    return 1;
  }
#endif

  std::signal(SIGINT, obsluz_sygnal);

  std::cout << "Starting stress test with " << watki << " threads to " << host << ":" << port
            << " (delay " << opoznienie_ms << " ms).";
  if (tryb_sync) {
    std::cout << " Synchronized burst mode enabled.";
  }
  if (czas_trwania_s > 0) {
    std::cout << " Duration: " << czas_trwania_s << "s.";
  }
  std::cout << " Press Ctrl+C to stop.\n";

  std::atomic<uint64_t> lacznie_wyslano{0};
  std::vector<std::thread> pracownicy;
  pracownicy.reserve(static_cast<size_t>(watki));
  BarieraSynchronizacji bariera(watki);
  BarieraSynchronizacji* wskaznik_bariery = tryb_sync ? &bariera : nullptr;

  auto czas_startu = std::chrono::steady_clock::now();
  for (int i = 0; i < watki; ++i) {
    pracownicy.emplace_back(petla_pracownika, i + 1, host, port, opoznienie_ms, wskaznik_bariery,
                            std::ref(lacznie_wyslano));
  }

  while (uruchomione.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (czas_trwania_s > 0) {
      auto uplynelo = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - czas_startu);
      if (uplynelo.count() >= czas_trwania_s) {
        uruchomione.store(false);
      }
    }
  }
  if (wskaznik_bariery != nullptr) {
    wskaznik_bariery->zatrzymaj();
  }

  for (auto& pracownik : pracownicy) {
    if (pracownik.joinable()) {
      pracownik.join();
    }
  }

  std::cout << "Total messages sent: " << lacznie_wyslano.load() << "\n";

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
