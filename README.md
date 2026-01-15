# chatApp
Projekt systemy operacyjne semestr 5

## Opis
Prosty czat oparty o TCP z obsługą rozmów zespołowych i prywatnych. Serwer korzysta z wielowątkowości (wątek na klienta) i loguje wszystkie rozmowy do jednego pliku.

## Budowanie
```
cmake -S . -B build
cmake --build build
```
### Windows (MinGW)
W przypadku plików wykonywalnych zbudowanych przy użyciu MinGW mogą być wymagane biblioteki
`libgcc_s_seh-1.dll` oraz `libstdc++-6.dll`. Możesz:
- włączyć statyczne linkowanie runtime w CMake:
```
cmake -S . -B build -DCHATAPP_STATIC_MINGW_RUNTIME=ON
cmake --build build
```
- albo dołączyć te DLL-e obok pliku `.exe`.

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

### Test obciążeniowy
```
./build/chat_stress 127.0.0.1 5555 20 5 30
```
- pierwszy argument to adres serwera
- drugi argument to port
- trzeci argument to liczba wątków (domyślnie 10)
- czwarty argument to opóźnienie między wiadomościami w ms (domyślnie 10)
- piąty argument to czas testu w sekundach (0 = do przerwania Ctrl+C)

## Komendy
- `/name <nick>` — ustawienie nazwy użytkownika
- `/msg <user> <message>` — wiadomość prywatna do wybranego użytkownika
