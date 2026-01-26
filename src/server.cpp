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
using UchwytGniazda =
#ifdef _WIN32
    SOCKET;
#else
    int;
#endif

using TypDlugosciGniazda =
#ifdef _WIN32
    int;
#else
    socklen_t;
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

struct InformacjeKlienta {
  UchwytGniazda gniazdo;
  std::string nazwa;
  std::string pokoj;
};

struct InformacjePokoju {
  std::string nazwa;
  std::string haslo;
  UchwytGniazda wlasciciel;
  std::unordered_set<UchwytGniazda> czlonkowie;
};

std::unordered_map<UchwytGniazda, InformacjeKlienta> klienci;
std::mutex mutex_klientow;

std::unordered_map<std::string, InformacjePokoju> pokoje;
std::mutex mutex_pokoi;

std::mutex mutex_logu;
std::ofstream plik_logu;

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

std::string znacznik_czasu_teraz() {
  std::time_t teraz = std::time(nullptr);
  char bufor[64];
  std::strftime(bufor, sizeof(bufor), "%Y-%m-%d %H:%M:%S", std::localtime(&teraz));
  return bufor;
}

void zapisz_log(const std::string& wiadomosc) {
  std::lock_guard<std::mutex> blokada(mutex_logu);
  plik_logu << "[" << znacznik_czasu_teraz() << "] " << wiadomosc << '\n';
  plik_logu.flush();
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

void rozglos_wiadomosc(const std::string& wiadomosc,
                       UchwytGniazda wyklucz_gniazdo = kNieprawidloweGniazdo) {
  std::lock_guard<std::mutex> blokada(mutex_klientow);
  for (const auto& [gniazdo, klient] : klienci) {
    if (gniazdo == wyklucz_gniazdo) {
      continue;
    }
    wyslij_wszystko(gniazdo, wiadomosc);
  }
}

void wyslij_system(UchwytGniazda gniazdo, const std::string& wiadomosc) {
  wyslij_wszystko(gniazdo, "[system] " + wiadomosc + "\n");
}

void wyslij_przypisanie_pokoju(UchwytGniazda gniazdo, const std::string& nazwa_pokoju) {
  wyslij_wszystko(gniazdo, "ROOM|" + nazwa_pokoju + "\n");
}

std::string ladunek_listy_pokoi() {
  std::lock_guard<std::mutex> blokada(mutex_pokoi);
  std::ostringstream ladunek;
  ladunek << "ROOMS|";
  bool pierwszy = true;
  for (const auto& [nazwa, pokoj] : pokoje) {
    if (!pierwszy) {
      ladunek << "|";
    }
    ladunek << nazwa << "|" << (pokoj.haslo.empty() ? "open" : "locked");
    pierwszy = false;
  }
  ladunek << "\n";
  return ladunek.str();
}

void wyslij_liste_pokoi(UchwytGniazda gniazdo) {
  wyslij_wszystko(gniazdo, ladunek_listy_pokoi());
}

void rozglos_liste_pokoi() {
  std::string ladunek = ladunek_listy_pokoi();
  std::lock_guard<std::mutex> blokada(mutex_klientow);
  for (const auto& [gniazdo, klient] : klienci) {
    wyslij_wszystko(gniazdo, ladunek);
  }
}

std::string przytnij(const std::string& tekst) {
  size_t start = tekst.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t koniec = tekst.find_last_not_of(" \t\r\n");
  return tekst.substr(start, koniec - start + 1);
}

bool czy_nazwa_bota(const std::string& nazwa) {
  return nazwa.rfind("Bot", 0) == 0;
}

bool utworz_pokoj(const std::string& nazwa_pokoju,
                  const std::string& haslo,
                  UchwytGniazda wlasciciel) {
  std::lock_guard<std::mutex> blokada(mutex_pokoi);
  if (pokoje.find(nazwa_pokoju) != pokoje.end()) {
    return false;
  }
  pokoje.emplace(nazwa_pokoju, InformacjePokoju{nazwa_pokoju, haslo, wlasciciel, {}});
  return true;
}

bool dolacz_do_pokoju(UchwytGniazda klient,
                      const std::string& nazwa_pokoju,
                      const std::string& haslo) {
  std::lock_guard<std::mutex> blokada(mutex_pokoi);
  auto iter = pokoje.find(nazwa_pokoju);
  if (iter == pokoje.end()) {
    return false;
  }
  if (!iter->second.haslo.empty() && iter->second.haslo != haslo) {
    return false;
  }
  iter->second.czlonkowie.insert(klient);
  return true;
}

void opusc_pokoj(UchwytGniazda klient, const std::string& nazwa_pokoju) {
  std::lock_guard<std::mutex> blokada(mutex_pokoi);
  auto iter = pokoje.find(nazwa_pokoju);
  if (iter == pokoje.end()) {
    return;
  }
  iter->second.czlonkowie.erase(klient);
}

enum class WynikUsunieciaPokoju {
  Sukces,
  NieZnaleziono,
  NieWlasciciel,
  Lobby,
};

WynikUsunieciaPokoju usun_pokoj(const std::string& nazwa_pokoju,
                               UchwytGniazda proszacy,
                               std::vector<UchwytGniazda>* czlonkowie) {
  std::lock_guard<std::mutex> blokada(mutex_pokoi);
  auto iter = pokoje.find(nazwa_pokoju);
  if (iter == pokoje.end()) {
    return WynikUsunieciaPokoju::NieZnaleziono;
  }
  if (nazwa_pokoju == "Lobby") {
    return WynikUsunieciaPokoju::Lobby;
  }
  if (iter->second.wlasciciel != proszacy) {
    return WynikUsunieciaPokoju::NieWlasciciel;
  }
  czlonkowie->assign(iter->second.czlonkowie.begin(), iter->second.czlonkowie.end());
  pokoje.erase(iter);
  return WynikUsunieciaPokoju::Sukces;
}

void rozglos_wiadomosc_pokoju(const std::string& nazwa_pokoju,
                             const std::string& wiadomosc,
                             UchwytGniazda wyklucz_gniazdo = kNieprawidloweGniazdo) {
  std::lock_guard<std::mutex> blokada(mutex_pokoi);
  auto iter = pokoje.find(nazwa_pokoju);
  if (iter == pokoje.end()) {
    return;
  }
  for (UchwytGniazda gniazdo : iter->second.czlonkowie) {
    if (gniazdo == wyklucz_gniazdo) {
      continue;
    }
    wyslij_wszystko(gniazdo, wiadomosc);
  }
}

void obsluz_prywatna_wiadomosc(UchwytGniazda nadawca,
                              const std::string& nazwa_nadawcy,
                              const std::string& komenda) {
  std::istringstream strumien(komenda);
  std::string token;
  strumien >> token;
  std::string nazwa_odbiorcy;
  strumien >> nazwa_odbiorcy;
  std::string wiadomosc;
  std::getline(strumien, wiadomosc);
  wiadomosc = przytnij(wiadomosc);

  if (nazwa_odbiorcy.empty() || wiadomosc.empty()) {
    wyslij_system(nadawca, "Użycie: /msg <użytkownik> <wiadomość>");
    return;
  }

  UchwytGniazda gniazdo_odbiorcy = kNieprawidloweGniazdo;
  {
    std::lock_guard<std::mutex> blokada(mutex_klientow);
    for (const auto& [gniazdo, klient] : klienci) {
      if (klient.nazwa == nazwa_odbiorcy) {
        gniazdo_odbiorcy = gniazdo;
        break;
      }
    }
  }

  if (gniazdo_odbiorcy == kNieprawidloweGniazdo) {
    wyslij_system(nadawca, "Nie znaleziono użytkownika: " + nazwa_odbiorcy);
    return;
  }

  std::string sformatowana = "[private] " + nazwa_nadawcy + ": " + wiadomosc + "\n";
  wyslij_wszystko(gniazdo_odbiorcy, sformatowana);
  wyslij_wszystko(nadawca, sformatowana);
  zapisz_log("[private] " + nazwa_nadawcy + " -> " + nazwa_odbiorcy + ": " + wiadomosc);
}

void obsluz_klienta(UchwytGniazda gniazdo, int id_klienta) {
  std::string nazwa_klienta = "gość" + std::to_string(id_klienta);
  {
    std::lock_guard<std::mutex> blokada(mutex_klientow);
    klienci[gniazdo] = {gniazdo, nazwa_klienta, "Lobby"};
  }
  dolacz_do_pokoju(gniazdo, "Lobby", "");
  wyslij_przypisanie_pokoju(gniazdo, "Lobby");
  wyslij_liste_pokoi(gniazdo);

  wyslij_system(gniazdo, "Witaj! Ustaw nazwę poleceniem /name <nick>.");
  wyslij_system(gniazdo, "Użyj /msg <użytkownik> <wiadomość> do prywatnych czatów.");
  wyslij_system(gniazdo,
                "Pokoje: /create <pokój> [hasło], /join <pokój> [hasło], /leave, /delete <pokój>.");

  rozglos_liste_pokoi();

  zapisz_log(nazwa_klienta + " dołączył do pokoju Lobby.");

  std::string przychodzace;
  char bufor[1024];
  while (uruchomione.load()) {
    std::memset(bufor, 0, sizeof(bufor));
    RozmiarGniazda odebrano = recv(gniazdo, bufor, sizeof(bufor) - 1, 0);
    if (odebrano <= 0) {
      break;
    }
    przychodzace.append(bufor, static_cast<size_t>(odebrano));
    size_t indeks_nowej_linii = przychodzace.find('\n');
    while (indeks_nowej_linii != std::string::npos) {
      std::string linia = przytnij(przychodzace.substr(0, indeks_nowej_linii));
      przychodzace.erase(0, indeks_nowej_linii + 1);
      indeks_nowej_linii = przychodzace.find('\n');
      if (linia.empty()) {
        continue;
      }

      if (linia.rfind("/name ", 0) == 0) {
        std::string nowa_nazwa = przytnij(linia.substr(6));
        if (nowa_nazwa.empty()) {
          wyslij_system(gniazdo, "Nazwa nie może być pusta.");
          continue;
        }
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          for (const auto& [gniazdo_klienta, klient] : klienci) {
            if (klient.nazwa == nowa_nazwa) {
              wyslij_system(gniazdo, "Nazwa jest już zajęta.");
              nowa_nazwa.clear();
              break;
            }
          }
          if (!nowa_nazwa.empty()) {
            klienci[gniazdo].nazwa = nowa_nazwa;
          }
        }
        if (!nowa_nazwa.empty()) {
          if (!czy_nazwa_bota(nowa_nazwa)) {
            rozglos_wiadomosc(
                "[system] " + nazwa_klienta + " ma teraz nazwę " + nowa_nazwa + ".\n");
          }
          zapisz_log(nazwa_klienta + " zmienił nazwę na " + nowa_nazwa);
          nazwa_klienta = nowa_nazwa;
        }
        continue;
      }

      if (linia.rfind("/msg ", 0) == 0) {
        obsluz_prywatna_wiadomosc(gniazdo, nazwa_klienta, linia);
        continue;
      }

      if (linia == "/rooms") {
        wyslij_liste_pokoi(gniazdo);
        continue;
      }

      if (linia.rfind("/create ", 0) == 0) {
        std::istringstream strumien(linia.substr(8));
        std::string nazwa_pokoju;
        std::string haslo;
        strumien >> nazwa_pokoju;
        strumien >> haslo;
        if (nazwa_pokoju.empty()) {
          wyslij_system(gniazdo, "Użycie: /create <pokój> [hasło]");
          continue;
        }
        if (!utworz_pokoj(nazwa_pokoju, haslo, gniazdo)) {
          wyslij_system(gniazdo, "Pokój już istnieje.");
          continue;
        }
        rozglos_liste_pokoi();
        std::string obecny_pokoj;
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          obecny_pokoj = klienci[gniazdo].pokoj;
        }
        if (!dolacz_do_pokoju(gniazdo, nazwa_pokoju, haslo)) {
          wyslij_system(gniazdo, "Pokój utworzony, ale nie udało się dołączyć.");
          continue;
        }
        if (!obecny_pokoj.empty() && obecny_pokoj != nazwa_pokoju) {
          opusc_pokoj(gniazdo, obecny_pokoj);
          if (!czy_nazwa_bota(nazwa_klienta)) {
            rozglos_wiadomosc_pokoju(
                obecny_pokoj, "[system] " + nazwa_klienta + " opuścił pokój.\n", gniazdo);
          }
        }
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          klienci[gniazdo].pokoj = nazwa_pokoju;
        }
        wyslij_przypisanie_pokoju(gniazdo, nazwa_pokoju);
        if (!czy_nazwa_bota(nazwa_klienta)) {
          rozglos_wiadomosc_pokoju(
              nazwa_pokoju, "[system] " + nazwa_klienta + " dołączył do pokoju.\n", gniazdo);
        }
        zapisz_log(nazwa_klienta + " dołączył do pokoju " + nazwa_pokoju);
        wyslij_system(gniazdo, "Pokój utworzony i dołączono: " + nazwa_pokoju);
        continue;
      }

      if (linia.rfind("/join ", 0) == 0) {
        std::istringstream strumien(linia.substr(6));
        std::string nazwa_pokoju;
        std::string haslo;
        strumien >> nazwa_pokoju;
        strumien >> haslo;
        if (nazwa_pokoju.empty()) {
          wyslij_system(gniazdo, "Użycie: /join <pokój> [hasło]");
          continue;
        }
        std::string obecny_pokoj;
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          obecny_pokoj = klienci[gniazdo].pokoj;
        }
        if (!dolacz_do_pokoju(gniazdo, nazwa_pokoju, haslo)) {
          wyslij_system(gniazdo, "Nie można dołączyć do pokoju. Sprawdź nazwę lub hasło.");
          continue;
        }
        if (!obecny_pokoj.empty() && obecny_pokoj != nazwa_pokoju) {
          opusc_pokoj(gniazdo, obecny_pokoj);
          if (!czy_nazwa_bota(nazwa_klienta)) {
            rozglos_wiadomosc_pokoju(
                obecny_pokoj, "[system] " + nazwa_klienta + " opuścił pokój.\n", gniazdo);
          }
        }
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          klienci[gniazdo].pokoj = nazwa_pokoju;
        }
        wyslij_przypisanie_pokoju(gniazdo, nazwa_pokoju);
        if (!czy_nazwa_bota(nazwa_klienta)) {
          rozglos_wiadomosc_pokoju(
              nazwa_pokoju, "[system] " + nazwa_klienta + " dołączył do pokoju.\n", gniazdo);
        }
        zapisz_log(nazwa_klienta + " dołączył do pokoju " + nazwa_pokoju);
        continue;
      }

      if (linia.rfind("/delete ", 0) == 0) {
        std::istringstream strumien(linia.substr(8));
        std::string nazwa_pokoju;
        strumien >> nazwa_pokoju;
        if (nazwa_pokoju.empty()) {
          wyslij_system(gniazdo, "Użycie: /delete <pokój>");
          continue;
        }
        std::vector<UchwytGniazda> czlonkowie;
        WynikUsunieciaPokoju wynik = usun_pokoj(nazwa_pokoju, gniazdo, &czlonkowie);
        if (wynik == WynikUsunieciaPokoju::NieZnaleziono) {
          wyslij_system(gniazdo, "Nie znaleziono pokoju.");
          continue;
        }
        if (wynik == WynikUsunieciaPokoju::Lobby) {
          wyslij_system(gniazdo, "Lobby nie może zostać usunięte.");
          continue;
        }
        if (wynik == WynikUsunieciaPokoju::NieWlasciciel) {
          wyslij_system(gniazdo, "Tylko właściciel pokoju może go usunąć.");
          continue;
        }
        for (UchwytGniazda gniazdo_czlonka : czlonkowie) {
          {
            std::lock_guard<std::mutex> blokada(mutex_klientow);
            auto iter = klienci.find(gniazdo_czlonka);
            if (iter != klienci.end()) {
              iter->second.pokoj = "Lobby";
            }
          }
          dolacz_do_pokoju(gniazdo_czlonka, "Lobby", "");
          wyslij_przypisanie_pokoju(gniazdo_czlonka, "Lobby");
          wyslij_system(gniazdo_czlonka, "Pokój usunięty. Przeniesiono Cię do Lobby.");
        }
        rozglos_liste_pokoi();
        zapisz_log(nazwa_klienta + " usunął pokój " + nazwa_pokoju);
        continue;
      }

      if (linia == "/leave") {
        std::string obecny_pokoj;
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          obecny_pokoj = klienci[gniazdo].pokoj;
        }
        if (obecny_pokoj.empty() || obecny_pokoj == "Lobby") {
          wyslij_system(gniazdo, "Już jesteś w Lobby.");
          continue;
        }
        opusc_pokoj(gniazdo, obecny_pokoj);
        rozglos_wiadomosc_pokoju(
            obecny_pokoj, "[system] " + nazwa_klienta + " opuścił pokój.\n", gniazdo);
        dolacz_do_pokoju(gniazdo, "Lobby", "");
        {
          std::lock_guard<std::mutex> blokada(mutex_klientow);
          klienci[gniazdo].pokoj = "Lobby";
        }
        wyslij_przypisanie_pokoju(gniazdo, "Lobby");
        wyslij_system(gniazdo, "Przeniesiono do Lobby.");
        continue;
      }

      std::string obecny_pokoj;
      {
        std::lock_guard<std::mutex> blokada(mutex_klientow);
        obecny_pokoj = klienci[gniazdo].pokoj;
      }

      if (obecny_pokoj.empty()) {
        wyslij_system(gniazdo, "Dołącz do pokoju zanim zaczniesz pisać.");
        continue;
      }

      std::string sformatowana =
          "[" + obecny_pokoj + "] " + nazwa_klienta + ": " + linia + "\n";
      rozglos_wiadomosc_pokoju(obecny_pokoj, sformatowana);
      zapisz_log("[" + obecny_pokoj + "] " + nazwa_klienta + ": " + linia);
    }
  }

  std::string obecny_pokoj;
  {
    std::lock_guard<std::mutex> blokada(mutex_klientow);
    obecny_pokoj = klienci[gniazdo].pokoj;
    klienci.erase(gniazdo);
  }
  if (!obecny_pokoj.empty()) {
    opusc_pokoj(gniazdo, obecny_pokoj);
    rozglos_wiadomosc_pokoju(
        obecny_pokoj, "[system] " + nazwa_klienta + " opuścił pokój.\n", gniazdo);
  }
  zamknij_gniazdo(gniazdo);
  rozglos_wiadomosc("[system] " + nazwa_klienta + " opuścił czat.\n");
  zapisz_log(nazwa_klienta + " opuścił czat.");
}

void obsluz_sygnal(int) {
  uruchomione.store(false);
}
}  // namespace

int main(int liczba_argumentow, char* argumenty[]) {
  int port = 5555;
  std::string sciezka_logu = "chat.log";
  if (liczba_argumentow >= 2) {
    port = std::stoi(argumenty[1]);
  }
  if (liczba_argumentow >= 3) {
    sciezka_logu = argumenty[2];
  }

  plik_logu.open(sciezka_logu, std::ios::app);
  if (!plik_logu) {
    std::cerr << "Nie można otworzyć pliku logu: " << sciezka_logu << "\n";
    return 1;
  }

  std::signal(SIGINT, obsluz_sygnal);

#ifdef _WIN32
  WSADATA dane_wsa;
  if (WSAStartup(MAKEWORD(2, 2), &dane_wsa) != 0) {
    std::cerr << "WSAStartup failed: " << tekst_bledu_gniazda() << "\n";
    return 1;
  }
#endif

  {
    std::lock_guard<std::mutex> blokada(mutex_pokoi);
    pokoje.emplace("Lobby", InformacjePokoju{"Lobby", "", kNieprawidloweGniazdo, {}});
  }

  UchwytGniazda gniazdo_serwera = socket(AF_INET, SOCK_STREAM, 0);
  if (gniazdo_serwera == kNieprawidloweGniazdo) {
    std::cerr << "Błąd gniazda: " << tekst_bledu_gniazda() << "\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  int opcja = 1;
#ifdef _WIN32
  setsockopt(gniazdo_serwera,
             SOL_SOCKET,
             SO_REUSEADDR,
             reinterpret_cast<const char*>(&opcja),
             static_cast<int>(sizeof(opcja)));
#else
  setsockopt(gniazdo_serwera, SOL_SOCKET, SO_REUSEADDR, &opcja, sizeof(opcja));
#endif

  sockaddr_in adres{};
  adres.sin_family = AF_INET;
  adres.sin_addr.s_addr = INADDR_ANY;
  adres.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(gniazdo_serwera, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) < 0) {
    std::cerr << "Błąd bind: " << tekst_bledu_gniazda() << "\n";
    zamknij_gniazdo(gniazdo_serwera);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  if (listen(gniazdo_serwera, 10) < 0) {
    std::cerr << "Błąd listen: " << tekst_bledu_gniazda() << "\n";
    zamknij_gniazdo(gniazdo_serwera);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  std::cout << "Serwer czatu uruchomiony na porcie " << port
            << ". Plik logu: " << sciezka_logu << "\n";

  int id_klienta = 1;
  while (uruchomione.load()) {
    sockaddr_in adres_klienta{};
    TypDlugosciGniazda dlugosc_klienta = sizeof(adres_klienta);
    UchwytGniazda gniazdo_klienta = accept(gniazdo_serwera,
                                          reinterpret_cast<sockaddr*>(&adres_klienta),
                                          &dlugosc_klienta);
    if (gniazdo_klienta == kNieprawidloweGniazdo) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEINTR) {
        continue;
      }
#else
      if (errno == EINTR) {
        continue;
      }
#endif
      std::cerr << "Błąd accept: " << tekst_bledu_gniazda() << "\n";
      break;
    }

    std::thread(obsluz_klienta, gniazdo_klienta, id_klienta++).detach();
  }

  zamknij_gniazdo(gniazdo_serwera);
  zapisz_log("Zamykanie serwera.");
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
