# chatApp
Projekt systemy operacyjne semestr 5

## Opis
Prosty czat oparty o TCP z obsługą rozmów zespołowych i prywatnych. Serwer korzysta z wielowątkowości (wątek na klienta) i loguje wszystkie rozmowy do jednego pliku.

## Budowanie
```
cmake -S . -B build
cmake --build build
```

## Uruchamianie
### Serwer
```
./build/chat_server 5555 chat.log
```
- pierwszy argument to port (domyślnie 5555)
- drugi argument to ścieżka do pliku logu (domyślnie `chat.log`)

### Klient
```
./build/chat_client 127.0.0.1 5555
```
- pierwszy argument to adres serwera
- drugi argument to port

## Komendy
- `/name <nick>` — ustawienie nazwy użytkownika
- `/msg <user> <message>` — wiadomość prywatna do wybranego użytkownika
