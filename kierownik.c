#include "common.h"
#include <sys/wait.h>
#include <sys/select.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

/*
 * ============================
 * KIEROWNIK: STEROWANIE i SYGNAŁY 1/2/3
 * ============================
 * Komendy:
 *  - 1: BLOKADA sektora (pracownik ustawia blokada_sektora[sektor]=1 w shm)
 *  - 2: ODBLOKOWANIE sektora (blokada_sektora[sektor]=0)
 *  - 3: EWAKUACJA globalna (ustawia ewakuacja_trwa=1 + pracownicy opróżniają sektory)
 */

/*
 * Proces kierownika steruje symulacją:
 *  - uruchamia zegar meczu,
 *  - przyjmuje komendy stop/start sektora, ewakuacja,
 *  - uruchamia ewakuację, wysyła polecenia do pracowników i zbiera raporty
*/

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#define MSGTYPE_KIEROWNIK_CTRL 5000

static int read_int_line(const char *prompt, int *out) {
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    char buf[128];
    if (!fgets(buf, sizeof(buf), stdin)) {
        return -1;
    }

    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;

    errno = 0;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (p == end) return 0;

    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return 0;

    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return 0;
    *out = (int)v;
    return 1;
}

static int try_become_master(int semid) {
    struct sembuf op;
    op.sem_num = SEM_KIEROWNIK;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT | SEM_UNDO;

    // Synchronizujemy się semaforem – pilnujemy kolejności i wykluczeń między procesami
    if (semop(semid, &op, 1) == 0) return 1;
    if (errno == EAGAIN) return 0;
    // Kończymy z komunikatem o błędzie
    die_errno("semop(SEM_KIEROWNIK)");
    return 0;
}

static void send_to_master(int msgid, int cmd, int sektor) {
    MsgSterujacy c = {MSGTYPE_KIEROWNIK_CTRL, cmd, sektor};

    /* msgsnd(): wysyła komunikat do kolejki*/
    if (msgsnd(msgid, &c, sizeof(int) * 2, 0) == -1) {
        if (errno == EIDRM || errno == EINVAL) {
            printf("[KONTROLER] Brak działającej symulacji (kolejka skasowana).\n");
            fflush(stdout);
            return;
        }
        warn_errno("msgsnd(ctrl->master)");
    }
}

/*
 * Bilety/odmowy dla kibiców idą NA OSOBNĄ KOLEJKĘ (KEY_MSG_TICKET).
 * Jeśli wyślemy je na KEY_MSG (kolejka żądań), kibice będą wisieć w msgrcv()
 * i cała symulacja nie dojdzie do końca (brak ./clean).
 */
static void send_ticket(int msgid_ticket, int kibic_id, int sektor) {
    MsgBilet msg;
    msg.mtype = MSGTYPE_TICKET_BASE + kibic_id;
    msg.sektor_id = sektor;

    while (msgsnd(msgid_ticket, &msg, sizeof(int), 0) == -1) {
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) return;
        warn_errno("msgsnd(send_ticket@kierownik)");
        return;
    }
}

static void cancel_queue_type(int msgid_req, int msgid_ticket, long req_type) {
    MsgKolejka req;
    while (1) {
        // Odbieramy wiadomość z kolejki
        ssize_t r = msgrcv(msgid_req, &req, sizeof(int), req_type, IPC_NOWAIT);
        if (r >= 0) {
            send_ticket(msgid_ticket, req.kibic_id, -1);
            continue;
        }
        if (errno == ENOMSG) break;
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) break;
        warn_errno("msgrcv(cancel_queue_type@kierownik)");
        break;
    }
}

static void ewakuacja(int msgid_req, int msgid_ticket, int semid, SharedState *stan) {
/*
 * =====================
 * EWAKUACJA
 * =====================
 * To jest główna „procedura bezpieczeństwa”.
 * Ustawiamy flagę ewakuacji w shm, żeby:
 *  - kibice przestali próbować wchodzić i zaczęli kończyć pętle,
 *  - kasjerzy przestali sprzedawać,
 *  - pracownicy sektorów wiedzieli, że mają opróżnić sektor.
 *
 * Następnie wysyłamy do każdego sektora MsgSterujacy:
 *  mtype=10+sektor, typ_sygnalu=3.
 *
 * Pracownicy odsyłają raporty mtype=99, kiedy:
 *  - obie bramki sektora są puste,
 *  - obecni_w_sektorze[sektor]==0.
 */

    /* Start ewakuacji + mecz zakończony*/
    // Ogłaszamy ewakuację
    stan->ewakuacja_trwa = 1;
    // Ustawiamy status meczu
    stan->status_meczu = 2;
    // Aktualizujemy odliczanie
    stan->czas_pozostaly = 0;

    union semun a;
    a.val = 0;
    // Ustawiamy wartość semafora
    if (semctl(semid, SEM_EWAKUACJA, SETVAL, a) == -1) {
        if (errno == EIDRM || errno == EINVAL) return;
        warn_errno("semctl");
    }

    // Iterujemy po wszystkich sektorach
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        a.val = 0;
        // Ustawiamy wartość semafora
        if (semctl(semid, SEM_SEKTOR_BLOCK_START + i, SETVAL, a) == -1) {
            if (errno == EIDRM || errno == EINVAL) return;
            warn_errno("semctl");
        }
    }

    cancel_queue_type(msgid_req, msgid_ticket, MSGTYPE_VIP_REQ);
    cancel_queue_type(msgid_req, msgid_ticket, MSGTYPE_STD_REQ);

    // Iterujemy po wszystkich sektorach
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        MsgSterujacy msg = {10 + i, 3, i};
        // Wysyłamy wiadomość do kolejki
        if (msgsnd(msgid_req, &msg, sizeof(int) * 2, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) return;
            warn_errno("msgsnd(ewakuacja)");
        }
    }

    printf("[KIEROWNIK] EWAKUACJA\n");
    fflush(stdout);

    /* Czekamy aż każdy pracownik odeśle raport sektor pusty*/
    int raporty = 0;
    while (raporty < LICZBA_SEKTOROW) {
        MsgSterujacy rap;
        // Odbieramy wiadomość z kolejki
        ssize_t res = msgrcv(msgid_req, &rap, sizeof(int) * 2, 99, 0);
        if (res >= 0) {
            printf("[RAPORT] Sektor %d pusty\n", rap.sektor_id);
            fflush(stdout);
            raporty++;
        } else {
            if (errno == EIDRM || errno == EINVAL) break; // kolejka skasowana
            if (errno == EINTR) continue;
            warn_errno("msgrcv(raport)");
            break;
        }
    }

    // Sprawdzamy ilu ludzi siedzi w sektorze
    while (stan->obecni_w_sektorze[SEKTOR_VIP] > 0) {
        usleep(10000);
    }

    printf("[KIEROWNIK] Koniec symulacji\n");
    fflush(stdout);
}

static pid_t start_clock_process(SharedState *stan, int semid) {
    // Sprawdzamy czy wolno jeszcze tworzyć procesy
    if (!reserve_process_slot(stan, semid)) {
        printf("Limit procesow osiagniety\n");
        fflush(stdout);
        return -1;
    }
    /* fork(): tworzy proces potomny, tu: zegar*/
    pid_t zegar_pid = fork();
    if (zegar_pid == -1) {
        // Cofamy rezerwację miejsca na proces
        rollback_process_slot(stan, semid);
        // Kończymy z komunikatem o błędzie
        die_errno("fork(zegar)");
    }
    if (zegar_pid != 0) return zegar_pid;

    /* Dziecko: aktualizuje stan czasu w shm*/
    time_t start = time(NULL);
    // Kończymy z komunikatem o błędzie
    if (start == (time_t)-1) die_errno("time");

    while (1) {
        time_t now = time(NULL);
        // Kończymy z komunikatem o błędzie
        if (now == (time_t)-1) die_errno("time");
        int left = CZAS_PRZED_MECZEM - (int)(now - start);
        if (left <= 0) break;
        // Aktualizujemy odliczanie
        stan->czas_pozostaly = left;
        sleep(1);
    }

    /* Start meczu*/
    // Ustawiamy status meczu
    stan->status_meczu = 1;
    // Aktualizujemy odliczanie
    stan->czas_pozostaly = CZAS_MECZU;

    start = time(NULL);
    // Kończymy z komunikatem o błędzie
    if (start == (time_t)-1) die_errno("time");

    while (1) {
        time_t now = time(NULL);
        // Kończymy z komunikatem o błędzie
        if (now == (time_t)-1) die_errno("time");
        int left = CZAS_MECZU - (int)(now - start);
        if (left <= 0) break;
        // Aktualizujemy odliczanie
        stan->czas_pozostaly = left;
        sleep(1);
    }

    /* Koniec meczu zegar kończy działanie*/
    // Ustawiamy status meczu
    stan->status_meczu = 2;
    // Aktualizujemy odliczanie
    stan->czas_pozostaly = 0;

    /* exit(): kończy proces potomny*/
    exit(0);
}

/*
 * ==========================
 * OBSŁUGA SYGNAŁÓW 1/2/3
 * ==========================
 * cmd==1 lub 2:
 *  - wysyłamy MsgSterujacy do konkretnego pracownika sektora:
 *      mtype = 10 + sektor
 *      typ_sygnalu = 1 (blokuj) lub 2 (odblokuj)
 *
 * cmd==3:
 *  - natychmiastowa ewakuacja:
 *      1) zatrzymujemy proces zegara,
 *      2) ustawiamy ewakuacja_trwa=1 w shm,
 *      3) rozsyłamy sygnał 3 do wszystkich sektorów,
 *      4) czekamy na raporty mtype=99 „sektor pusty”.
 */

    /* Sygnał 3: natychmiastowa ewakuacja zatrzymujemy zegar*/
static int handle_cmd_master(int msgid_req, int msgid_ticket, int semid, SharedState *stan, pid_t *zegar_pid, int cmd, int sektor) {
    if (cmd == 3) {
        if (*zegar_pid > 0) {
            /* kill(): wysyła sygnał SIGTERM do procesu zegara*/
            if (kill(*zegar_pid, SIGTERM) == -1 && errno != ESRCH) warn_errno("kill(zegar)");

            /* waitpid(): czekamy aż zegar się zakończy*/
            while (waitpid(*zegar_pid, NULL, 0) == -1 && errno == EINTR) {}
            *zegar_pid = -1;
        }

        ewakuacja(msgid_req, msgid_ticket, semid, stan);
        return 1; /* koniec */
    }

    /* Sygnały 1/2: blokuj/odblokuj konkretny sektor*/
    if (cmd == 1 || cmd == 2) {
        if (sektor < 0 || sektor >= LICZBA_SEKTOROW) {
            printf("[KIEROWNIK] Błąd: sektor musi być liczbą 0-7\n");
            fflush(stdout);
            return 0;
        }

        MsgSterujacy msg = {10 + sektor, cmd, sektor};

        /* msgsnd(): wysyła polecenie sterowania do pracownika sektora*/
        if (msgsnd(msgid_req, &msg, sizeof(int) * 2, 0) == -1) {
            if (!(errno == EIDRM || errno == EINVAL)) warn_errno("msgsnd(sterowanie)");
        }
        return 0;
    }

    printf("[KIEROWNIK] Błąd: wpisz 1, 2 albo 3\n");
    fflush(stdout);
    return 0;
}

int main() {
    setbuf(stdout, NULL);

    /* msgget(): pobiera kolejkę żądań (kibic -> kasjer, oraz sterowanie sektorami) */
    int msgid_req = msgget(KEY_MSG, 0600);
    // Kończymy z komunikatem o błędzie
    if (msgid_req == -1) die_errno("msgget(req)");

    /* msgget(): pobiera kolejkę biletów (kasjer/kierownik -> kibic) */
    int msgid_ticket = msgget(KEY_MSG_TICKET, 0600);
    // Kończymy z komunikatem o błędzie
    if (msgid_ticket == -1) die_errno("msgget(ticket)");

    /* semget(): pobiera istniejący zestaw semaforów*/
    int semid = semget(KEY_SEM, 0, 0600);
    // Kończymy z komunikatem o błędzie
    if (semid == -1) die_errno("semget");

    /* shmget(): pobiera segment pamięci współdzielonej*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    // Kończymy z komunikatem o błędzie
    if (shmid == -1) die_errno("shmget");

    /* shmat(): mapuje shm do pamięci procesu*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    // Kończymy z komunikatem o błędzie
    if (stan == (void*)-1) die_errno("shmat");

/*
 * =============================
 * WYBÓR MASTER-KIEROWNIKA
 * =============================
 * Używamy osobnego semafora jako mutexu na rolę mastera.
 * Pierwszy kierownik, który wykona semop(-1) bez blokowania, zostaje masterem.
 *
 * SEM_UNDO:
 *  - jeśli master padnie, kernel cofnie operację semafora,
 *    więc kolejny uruchomiony kierownik może zostać masterem.
 */

    /* Próba zostania masterem: tylko master uruchamia zegar */
    int is_master = try_become_master(semid);

    if (!is_master) {
        /*
         * KONTROLER: nie uruchamia zegara i nie zmienia stanu meczu.
         * Tylko zbiera komendy z klawiatury i przesyła je do mastera.
         */
        printf("[KONTROLER] Master-kierownik już działa. Wysyłam tylko komendy.\n");
        printf("Komendy: 1-stop, 2-start, 3-ewakuacja\n");
        fflush(stdout);

        while (1) {
            int cmd;
            int rc = read_int_line(NULL, &cmd);
            if (rc == -1) break;
            if (rc == 0 || (cmd != 1 && cmd != 2 && cmd != 3)) {
                printf("[KONTROLER] Błąd: wpisz 1, 2 albo 3\n");
                fflush(stdout);
                continue;
            }

            if (cmd == 3) {
                send_to_master(msgid_req, 3, -1);
                continue;
            }

            int s;
            int rs = read_int_line("Sektor (0-7): ", &s);
            if (rs == -1) break;
            if (rs != 1 || s < 0 || s >= LICZBA_SEKTOROW) {
                printf("[KONTROLER] Błąd: sektor musi być liczbą 0-7\n");
                fflush(stdout);
                continue;
            }

            send_to_master(msgid_req, cmd, s);
        }

        /* shmdt(): odłącza shm od procesu kierownika*/
        if (shmdt(stan) == -1) warn_errno("shmdt");
        return 0;
    }

    /* Stan początkowy (tylko jeśli setup wyzerował stan)*/
    // Sprawdzamy czy trwa ewakuacja
    if (stan->status_meczu == 0 && stan->czas_pozostaly == 0 && !stan->ewakuacja_trwa) {
        // Ustawiamy status meczu
        stan->status_meczu = 0;
        // Aktualizujemy odliczanie
        stan->czas_pozostaly = CZAS_PRZED_MECZEM;
    }

    /* Zegar meczu uruchamia tylko master*/
    pid_t zegar_pid = -1;
    // Sprawdzamy czy trwa ewakuacja
    if (!stan->ewakuacja_trwa && stan->status_meczu == 0) {
        zegar_pid = start_clock_process(stan, semid);
    }

    printf("Komendy: 1-stop, 2-start, 3-ewakuacja\n");
    fflush(stdout);

    fd_set readfds;
    struct timeval tv;

    while (1) {
        /*
         * Sprawdzamy, czy zegar już się zakończył:
         * jeśli tak -> automatyczna ewakuacja
         */
        if (zegar_pid > 0) {
            while (1) {
                // Zbieramy zakończone procesy potomne
                pid_t w = waitpid(zegar_pid, NULL, WNOHANG);
                if (w > 0) { ewakuacja(msgid_req, msgid_ticket, semid, stan); goto out; }
                if (w == 0) break;
                if (errno == EINTR) continue;
                warn_errno("waitpid(WNOHANG)");
                break;
            }
        }

        /* Odbiór komend od kontrolerów (nie blokuj) */
        while (1) {
            MsgSterujacy c;
            /* msgrcv(): odbiera komunikat z kolejki*/
            ssize_t r = msgrcv(msgid_req, &c, sizeof(int) * 2, MSGTYPE_KIEROWNIK_CTRL, IPC_NOWAIT);
            if (r >= 0) {
                if (handle_cmd_master(msgid_req, msgid_ticket, semid, stan, &zegar_pid, c.typ_sygnalu, c.sektor_id)) goto out;
                continue;
            }

            if (errno == ENOMSG) break;
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) goto out;
            warn_errno("msgrcv(ctrl)");
            break;
        }

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            if (errno == EINTR) continue;
            warn_errno("select");
            continue;
        }

        if (ret > 0) {
            int cmd;
            int rc = read_int_line(NULL, &cmd);
            if (rc == -1) break;

            if (rc == 0) {
                printf("[KIEROWNIK] Błąd: wpisz 1, 2 albo 3\n");
                fflush(stdout);
                continue;
            }

            if (cmd == 3) {
                if (handle_cmd_master(msgid_req, msgid_ticket, semid, stan, &zegar_pid, 3, -1)) break;
                continue;
            }

            if (cmd == 1 || cmd == 2) {
                int s;
                int rs = read_int_line("Sektor (0-7): ", &s);
                if (rs == -1) break;
                if (rs != 1) {
                    printf("[KIEROWNIK] Błąd: sektor musi być liczbą 0-7\n");
                    fflush(stdout);
                    continue;
                }

                if (handle_cmd_master(msgid_req, msgid_ticket, semid, stan, &zegar_pid, cmd, s)) break;
                continue;
            }

            printf("[KIEROWNIK] Błąd: wpisz 1, 2 albo 3\n");
            fflush(stdout);
        }
    }

out:
    /* shmdt(): odłącza shm od procesu kierownika*/
    if (shmdt(stan) == -1) warn_errno("shmdt");

    /* Na wszelki wypadek dobijam zegar jeśli jeszcze żyje
    kill(): wysyła sygnał do procesu*/
    if (zegar_pid > 0) {
        if (kill(zegar_pid, SIGTERM) == -1 && errno != ESRCH) warn_errno("kill(zegar)");

        /* waitpid(): sprząta proces potomny żeby nam się zombie nie zrobił*/
        while (waitpid(zegar_pid, NULL, 0) == -1 && errno == EINTR) {}
    }

    return 0;
}