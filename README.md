# Raport Projektu Systemy Operacyjne - Kaiten Zushi

## Metadane
**Autor:** Piotr Kątniak (Nr Albumu: 155187)  
**Link do repozytorium:** [https://github.com/skiper9812/kaiten_zushi/](https://github.com/skiper9812/kaiten_zushi/)  

### Środowisko oraz kompilator
**System Operacyjny:**
```bash
PRETTY_NAME="Debian GNU/Linux 11 (bullseye)"
NAME="Debian GNU/Linux"
VERSION_ID="11"
VERSION="11 (bullseye)"
VERSION_CODENAME=bullseye
ID=debian
```

**Kompilator:**
```bash
g++ (GCC) 8.5.0
Copyright (C) 2018 Free Software Foundation, Inc.
```

---

## Założenia projektowe i Uruchomienie

### Opis ogólny
Projekt symuluje działanie restauracji typu Kaiten Sushi (sushi na taśmie). System składa się z wielu procesów współpracujących ze sobą przy użyciu mechanizmów IPC (pamięć dzielona, semafory, kolejki komunikatów, łącza nazwane). Główny proces (`main`) i jego potomkowie zarządzają różnymi aspektami restauracji: Chef (kuchnia), Belt (taśma), Service (kelnerzy/obsługa), Manager (zarządzanie czasem/prędkością) oraz Klienci.

Wszystkie procesy są potomkami głównego programu `./restauracja` (brak wywołań `exec()` dla modułów wewnętrznych, logika rozwidlana jest przez `fork()`).

### Sposób uruchomienia
```bash
make
./restauracja
# (w osobnej konsoli można śledzić logi)
cat logs/simulation.log
```

Wszystkie procesy widoczne w `ps` będą występować pod nazwą `./restauracja`.

---

## Struktura Kodu

### Główne Procesy
*   **Main**: Inicjalizuje IPC, tworzy procesy potomne, obsługuje sygnały kończące.
*   **Manager**: Steruje otwarciem/zamknięciem restauracji i prędkością taśmy.
*   **Chef**: Produkuje dania i umieszcza je na taśmie.
*   **Belt**: Zarządza przesuwaniem dań na taśmie.
*   **Service**: Przypisuje grupy klientów do stolików (logika `assignTable`).
*   **Clients**: Generator klientów oraz wątki reprezentujące poszczególne osoby w grupach.

### Klasa Group
Reprezentuje grupę klientów. Zawiera informacje o ID grupy, liczbie osób, zamówieniach (dania premium vs zwykłe), statusie VIP oraz logikę konsumpcji.

### Osiągnięcia
*   Pełna synchronizacja wielu procesów i wątków bez zakleszczeń (deadlocków) w normalnym trybie pracy.
*   Implementacja "inteligentnego" kelnera (algorytm przydziału stolików uwzględniający dosiadanie się).
*   Obsługa sygnałów czasu rzeczywistego do sterowania symulacją (zmiana prędkości, pauza).

### Problemy
*   Skomplikowana synchronizacja przy "sprzątaniu" procesów zombie. Obecnie używany jest `reaperThread` z **blokującym** `waitpid`, co pozwala uniknąć aktywnego odpytywania (pollingu).
*   Trudności z poprawną obsługą sygnałów `SIGTERM` oraz `^C` (SIGINT), aby zapewnić spójność logów biznesowych (zgodność liczby `CREATED` i `FINISHED` groups) przy przerywaniu symulacji.
*   **Ograniczenia Monitora Czasu (Pauza):** Pełna obsługa sygnału `SIGSTOP` nie jest możliwa z poziomu aplikacji, ponieważ sygnał ten jest obsługiwany bezpośrednio przez jądro systemu i nie może być przechwycony ani zignorowany przez proces użytkownika. Zaimplementowany mechanizm kompensacji czasu opiera się na wątku monitorującym i sygnale `SIGCONT`, co stanowi aproksymację czasu przestoju. W rezultacie, synchronizacja zegara symulacji po wznowieniu procesu może być obarczona niewielkim błędem pomiarowym wynikającym z opóźnień w dostarczeniu sygnału wybudzenia.

---

## Pseudokod Kluczowych Funkcji

### 1. `assignTable` (Service)
Algorytm przydzielania stolika grupie klientów.
```cpp
function assignTable(state, groupSize, vipStatus, pid):
    for each table in state.tables:
        LOCK(state_mutex)
        
        if (vipStatus AND table.capacity == 1):
             UNLOCK(state_mutex)
             continue
             
        freeSeats = table.capacity - table.occupied
        if (freeSeats < groupSize):
             UNLOCK(state_mutex)
             continue
             
        // Sprawdzenie kompatybilności z obecnymi gośćmi (np. ten sam rozmiar grupy)
        if (guestsAtTable are compatible with newGroup):
             Assign slots to new group
             Update occupiedSeats
             UNLOCK(state_mutex)
             return tableIndex
             
        UNLOCK(state_mutex)
    return -1 (Brak Miejsc)
```

### 2. `tryAssignFromQueue` (Service)
Próba pobrania grupy z kolejki oczekujących i posadzenia jej przy stole.
```cpp
function tryAssignFromQueue(queue, lockingSemaphore):
    LOCK(queue_mutex)
    
    for each group in queue:
        if (ZombieTestActive AND ZombieBlocking):
             break
             
        tableID = assignTable(state, group.size, group.vip, group.pid)
        
        if (tableID != -1):
             Remove group from queue
             Send ASSIGN_CONFIRM message to client process
             UNLOCK(queue_mutex)
             SIGNAL(lockingSemaphore) // Zwolnienie miejsca w kolejce
             return TRUE
             
    UNLOCK(queue_mutex)
    return FALSE
```

### 3. `handleConsumeDish` (Client)
Logika konsumpcji pojedynczego dania przez klienta.
```cpp
function handleConsumeDish(group):
    LOCK(belt_mutex)
    
    myTableSlots = calculateBeltRangeForTable(group.tableID)
    
    for slot in myTableSlots:
        dish = belt[slot]
        
        if (dish.exists AND (dish.targetGroup == -1 OR dish.targetGroup == group.ID)):
             // Konsumpcja
             Update shared memory (soldCount, revenue)
             Remove dish from belt (dish.id = 0)
             
             SIGNAL(belt_slots_sem) // Powiadomienie o wolnym miejscu na taśmie
             UNLOCK(belt_mutex)
             return TRUE
             
    UNLOCK(belt_mutex)
    return FALSE
```

---

## Elementy Specjalne i Wyróżniające
1.  **Kolorowe Logi**: Użycie kodów ANSI w logach do szybkiego rozróżniania procesów (Service - zielony, Client - limonkowy, Chef - domyślny/inny).
2.  **Monitor Pauzy (SIGSTOP)**: Wątek `pauseMonitorThread` (ipc_manager.cpp), który mierzy rzeczywisty czas zatrzymania procesu po otrzymaniu SIGSTOP/SIGCONT i koryguje czas symulacji.
3.  **Szablony Kolejek (Templates)**: Generyczne funkcje `queueSend<T>` i `queueRecv<T>` w `ipc_manager.cpp` pozwalające na przesyłanie różnych struktur danych.

---

## Testy

> [!NOTE]
> **Uwaga dotycząca konfiguracji:** Wszystkie poniższe scenariusze testowe zakładają **wyłączność**. Włączenie jednego makra testowego (np. `#define TABLE_SHARING_TEST 1`) implikuje, że wszystkie pozostałe flagi testowe (takie jak `STRESS_TEST`, `ZOMBIE_TEST`) są ustawione na `0`. Wymaga to manualnej edycji pliku `common.h`.

<details>
<summary>TEST I: Stress Test (5K Clients)</summary>

**Opis:**
5000 Klientów oczekuje w kolejce > Kucharz gotuje aż taśma (300 slotów) będzie pełna > Kucharz zostaje zatrzymany (brak miejsca) > Klienci wchodzą i konsumują.

**Zmiany w konfiguracji:**
```cpp
#define STRESS_TEST 1
#define FIXED_GROUP_COUNT 5000 
#define MAX_QUEUE 5000
#define BELT_SIZE 300
```

**Efekt:**
Przy pustym wejściu na taśmę -> DEADLOCK symulacji (Kucharz czeka na miejsce, Klienci czekają na dania).

**Logi:**
Monitorowanie procesów (duża liczba procesów potomnych):
```
katniak+ 4015250  0.0  0.0  14052   196 pts/11   S+   00:37   0:00 ./restauracja
...
katniak.piotr.155187@torus:~/projektSO$ ps aux | grep restauracja | wc -l
5008 # main, logger, manager, service, belt, start_clients, chef + 5000 clients
```

Tworzenie grup (5000):
```
cat logs/simulation.log | grep CREATED | wc -l
5000
```

Konsumpcja i produkcja (500 dań - tyle ile udało się zjeść przed deadlockiem/zatrzymaniem lub w limitowanym oknie):
```
cat logs/simulation.log | grep CONSUMED | wc -l
500
cat logs/simulation.log | grep COOKED | wc -l
500
```

Walidacja końcowa:
```
========== VALIDATION ==========
Produced: 500
Sold:     500
Remaining:0
Wasted:   0
+ MATCH: Produced == Sold+Remaining+Wasted
```
</details>

<details>
<summary>TEST II: Zombie Test (50 Groups)</summary>

**Opis:**
50 Grup. Pierwsza wchodzi, reszta czeka. Grupa 0 zamawia 100 dań Premium. Kucharz gotuje. Grupa zjada JEDNO danie i wychodzi. Reszta wchodzi.

**Zmiany w konfiguracji:**
```cpp
#define FIXED_GROUP_COUNT 50
#define BELT_SIZE 100
#define ZOMBIE_TEST 1 // or 2
```

**Efekt:**
*   Konfiguracja 1: Bez czyszczenia dań (ZOMBIE_TEST 1) -> Symulacja trwa dłużej, ale nic się nie marnuje.
*   Konfiguracja 2: Z czyszczeniem dań (ZOMBIE_TEST 2) -> Symulacja kończy się błyskawicznie (1s), dania są usuwane.

**Logi:**
Konfiguracja 1:
```
symulacja trwała 6 sekund
========== SERVICE REPORT ==========
Products remaining on belt:
TOTAL remaining: 100 dishes, 4780 PLN value
```

Konfiguracja 2:
```
symulacja trwała 1 sekundę
========== WASTED REPORT ==========
TOTAL wasted: 99 dishes, 4990 PLN value
```
</details>

<details>
<summary>TEST III: Safe Signal Handling in Critical Section</summary>

**Opis:**
Weryfikacja odporności systemu na nagłe zakończenie (sygnał `SIGTERM`/`^C`) wewnątrz sekcji krytycznej `SEM_MUTEX_BELT` podczas konsumpcji. Klient pobiera semafor taśmy, zaczyna przetwarzać danie (aktualizacja pieniędzy), a następnie **sam** wysyła sygnał zabicia do całej grupy procesów (`kill(0, SIGTERM)`).

**Zmiany w konfiguracji:**
```cpp
#define CRITICAL_TEST 1
```

**Efekt:**
1.  Sygnał przerywa bieżącą instrukcję (lub `usleep`).
2.  Handler sygnału w procesie ustawia `terminate_flag = 1`.
3.  Proces **kontynuuje** wykonywanie funkcji: kończy zdejmowanie dania z taśmy i wykonuje `V(SEM_MUTEX_BELT)`.
4.  Dopiero po wyjściu z sekcji krytycznej proces kończy pracę.
5.  **Dzięki temu nie dochodzi do zakleszczenia (Deadlock) ani niespójności danych (pieniądze naliczone = danie zdjęte).**

**Logi:**
```
[CLIENTS]: CONSUMED DISH ...
!!! TRIGGERING SUICIDE SIGNAL IN CRITICAL SECTION !!!
[SERVICE]: SERVED ALL GROUPS ...
...
========== VALIDATION ==========
Produced: 127
Sold:     37
Remaining:90
Wasted:   0
----------------------------------
Sold+Remaining+Wasted = 127
+ MATCH: Produced == Sold+Remaining+Wasted
```
</details>

<details>
<summary>TEST IV: Table Sharing (Mixed-Size Blocking)</summary>

**Opis:**
Weryfikacja reguły równoliczności grup przy stoliku oraz mechanizmu współdzielenia stolików. System powinien pozwalać na siedzenie wielu grup przy jednym stoliku TYLKO wtedy, gdy są one tego samego rozmiaru.

**Zmiany w konfiguracji:**

*Przypadek 1 (Współdzielenie):*
```cpp
#define TABLE_SHARING_TEST 1
#define FIXED_GROUP_COUNT 40
// Stoliki tylko X3/X4
```

*Przypadek 2 (Blokowanie):*
```cpp
#define TABLE_SHARING_TEST 2
#define FIXED_GROUP_COUNT 2
// Tylko 1 stolik X3
// Grupa 0 (size 2), Grupa 1 (size 1)
```

**Efekt:**
1.  **Przypadek 1:** Stoliki są współdzielone przez wiele małych grup (np. trzy grupy 1-osobowe przy stoliku 3-osobowym).
2.  **Przypadek 2:** Mimo wolnych miejsc (stolik 3-osobowy, siedzą 2 osoby), system NIE DOKWATEROWUJE grupy 1-osobowej, ponieważ naruszyłoby to zasadę "równych grup". Grupa 1 czeka, aż Grupa 0 całkowicie zwolni stolik.

**Logi:**
Przypadek 1 (Współdzielenie):
```
// Sytuacja A: 3 grupy 1-osobowe przy stoliku 3-osobowym (ID=18)
[SERVICE]: QUEUE -> TABLE | tableID=18 pid=40079 groupID=36 size=1 vip=0
[SERVICE]: QUEUE -> TABLE | tableID=18 pid=40081 groupID=38 size=1 vip=0
[SERVICE]: QUEUE -> TABLE | tableID=18 pid=40083 groupID=40 size=1 vip=0

// Sytuacja B: 2 grupy 2-osobowe przy stoliku 4-osobowym (ID=10)
[SERVICE]: QUEUE -> TABLE | tableID=10 pid=40100 groupID=45 size=2 vip=0
[SERVICE]: QUEUE -> TABLE | tableID=10 pid=40150 groupID=70 size=2 vip=0
```
*(Stoliki są w pełni utylizowane przez grupy o sumarycznym rozmiarze równym pojemności)*

Przypadek 2 (Blokowanie):
```
[SERVICE]: TABLE ASSIGNED | tableID=0 pid=61171 groupID=0 size=2
...
[SERVICE]: GROUP PAID OFF | groupID=0 ...
[SERVICE]: QUEUE -> TABLE | tableID=0 pid=61172 groupID=1 size=1
```
*(Grupa 1 wchodzi dopiero po wyjściu Grupy 0)*
</details>

<details>
<summary>TEST V: Pełna Prawidłowa Symulacja (2500 Grup)</summary>

**Opis:**
Prawidłowo przeprowadzona pełna symulacja weryfikująca stabilność systemu, brak zakleszczeń (deadlocks), poprawność obsługi kolejek oraz spójność danych finansowych i magazynowych.

**Zmiany w konfiguracji:**
```cpp
#define SKIP_DELAYS 1
#define FIXED_GROUP_COUNT 2500
#define MAX_QUEUE 1000
#define BELT_SIZE 100
```

**Efekt:**
Symulacja przebiega płynnie do końca. Brak deadlocków, wszyscy klienci obsłużeni (CREATED == FINISHED), bilans produktów zgadza się co do sztuki.

**Logi:**
Weryfikacja liczby grup:
```
[SERVICE]: ALL 2500 GROUPS CREATED - OPENING ADMISSION GATES (SIGNAL)
...
cat logs/simulation.log | grep "GROUP CREATED" | wc -l
2500
cat logs/simulation.log | grep "GROUP FINISHED" | wc -l
2500
```

Raport końcowy (Walidacja):
```
========== VALIDATION ==========
Produced: 20043
Sold:     16430
Remaining:100
Wasted:   3513
----------------------------------
Sold+Remaining+Wasted = 20043
+ MATCH: Produced == Sold+Remaining+Wasted
```
</details>

---

## Linki do Kodu (System Calls)

Poniżej znajdują się odniesienia do plików źródłowych demonstrujące użycie wymaganych mechanizmów systemowych.
Link do repozytorium (commit `40d612d`): [https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/)

### a. Tworzenie i obsługa plików
*   [open()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L245): Otwarcie FIFO do zapisu.
*   [write()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L269): Zapis logów do FIFO.
*   [close()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L252): Zamknięcie deskryptora pliku.
*   [unlink()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L502): Usuwanie plików FIFO.

### b. Tworzenie procesów
*   [fork()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/client.cpp#L92): Tworzenie procesów grup klientów.
*   [_exit()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/client.cpp#L115): Bezpieczne wyjście z procesu potomnego.
*   [wait()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/main.cpp#L99): Oczekiwanie na procesy potomne w Main.
*   [waitpid()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/client.cpp#L37): Reaper Thread sprzątający procesy-dzieci.

### c. Tworzenie i obsługa wątków
*   [pthread_create()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/client.cpp#L422): Wątki reprezentujące pojedynczych klientów.
*   [pthread_join()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/client.cpp#L426): Oczekiwanie na zakończenie wątków klientów.
*   [pthread_sigmask()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L519): Maskowanie sygnałów w wątku monitora.

### d. Obsługa sygnałów
*   [kill()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/main.cpp#L14): Propagacja SIGTERM do grupy procesów.
*   [signal()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/main.cpp#L35): Ignorowanie SIGPIPE, SIGUSR.
*   [sigaction()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/main.cpp#L27): Rejestracja handlerów dla SIGINT/SIGTERM.

### e. Synchronizacja procesów (Semafore System V)
*   [semget()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L441): Tworzenie zbioru semaforów.
*   [semctl()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L445): Inicjalizacja wartości (SETVAL).
*   [semop()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L363): Operacja V (podnoszenie).
*   [semtimedop()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L332): Operacja P z timeoutem (waiting).

### f. Łącza nazwane (FIFO)
*   [mkfifo()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L233): Inicjalizacja kanału logowania.

### g. Pamięć dzielona (Shared Memory System V)
*   [shmget()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L435): Alokacja segmentu pamięci.
*   [shmat()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L438): Dołączanie pamięci do przestrzeni adresowej procesu.
*   [shmdt()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L494): Odłączanie pamięci.
*   [shmctl()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L495): Usuwanie segmentu (IPC_RMID).

### h. Kolejki komunikatów (Message Queues System V)
*   [msgget()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L49): Tworzenie kolejki komunikatów.
*   [msgsnd()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L88): Wysłanie komunikatu (non-blocking/retry w pętli).
*   [msgrcv()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L123): Odbiór komunikatu.
*   [msgctl()](https://github.com/skiper9812/kaiten_zushi/blob/40d612defe71f17ec8dad6174f7e5bb6a4289566/ipc_manager.cpp#L56): Usuwanie kolejki.
