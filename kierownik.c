#include "common.h"
#include <sys/wait.h>
#include <time.h>

/*
 * Proces „kierownika” steruje symulacją:
 *  - uruchamia zegar meczu,
 *  - przyjmuje komendy stop/start sektora, ewakuacja,
 *  - uruchamia ewakuację, wysyła polecenia do pracowników i zbiera raporty
 */

static void ewakuacja(int msgid, SharedState *stan) {
    /* Start ewakuacji + mecz zakończony*/
    stan->ewakuacja_trwa = 1;
    stan->status_meczu = 2;

    /* Wysyłamy do każdego pracownika sektora sygnał 3 = ewakuacja*/
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        MsgSterujacy msg = {10 + i, 3, i};

        /* msgsnd(): wysyła komunikat do kolejki*/
        if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) {
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

        /* msgrcv(): odbiera komunikat z kolejki*/
        ssize_t res = msgrcv(msgid, &rap, sizeof(int) * 2, 99, 0);
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

    /* Zerowanie sektora VIP*/
    while (stan->obecni_w_sektorze[SEKTOR_VIP] > 0) {
        usleep(100000);
    }

    printf("[KIEROWNIK] Koniec symulacji\n");
    fflush(stdout);
}

int main() {
    setbuf(stdout, NULL);

    /* msgget(): pobiera istniejącą kolejkę komunikatów*/
    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) die_errno("msgget");

    /* shmget(): pobiera segment pamięci współdzielonej*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) die_errno("shmget");

    /* shmat(): mapuje shm do pamięci procesu*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    /* Stan początkowy*/
    stan->status_meczu = 0;
    stan->czas_pozostaly = CZAS_PRZED_MECZEM;

    /*
     * Zegar meczu:
     *  - odlicza do startu,
     *  - potem odlicza czas meczu,
     */
    /* fork(): tworzy proces potomny, tu: zegar*/
    pid_t zegar_pid = fork();
    if (zegar_pid == -1) die_errno("fork(zegar)");
    if (zegar_pid == 0) {
        /* Dziecko: aktualizuje stan czasu w shm*/
        time_t start = time(NULL);
        if (start == (time_t)-1) die_errno("time");

        while (1) {
            time_t now = time(NULL);
            if (now == (time_t)-1) die_errno("time");
            int left = CZAS_PRZED_MECZEM - (int)(now - start);
            if (left <= 0) break;
            stan->czas_pozostaly = left;
            sleep(1);
        }

        /* Start meczu*/
        stan->status_meczu = 1;
        stan->czas_pozostaly = CZAS_MECZU;

        start = time(NULL);
        if (start == (time_t)-1) die_errno("time");

        while (1) {
            time_t now = time(NULL);
            if (now == (time_t)-1) die_errno("time");
            int left = CZAS_MECZU - (int)(now - start);
            if (left <= 0) break;
            stan->czas_pozostaly = left;
            sleep(1);
        }

        /* Koniec meczu zegar kończy działanie*/
        stan->status_meczu = 2;
        stan->czas_pozostaly = 0;

        /* exit(): kończy proces potomny*/
        exit(0);
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
        while (1) {
            pid_t w = waitpid(zegar_pid, NULL, WNOHANG);
            if (w > 0) { ewakuacja(msgid, stan); goto out; }
            if (w == 0) break;
            if (errno == EINTR) continue;
            warn_errno("waitpid(WNOHANG)");
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
            if (scanf("%d", &cmd) == 1) {

                /* Sygnał 3: natychmiastowa ewakuacja zatrzymujemy zegar*/
                if (cmd == 3) {
                    /* kill(): wysyła sygnał SIGTERM do procesu zegara*/
                    if (kill(zegar_pid, SIGTERM) == -1 && errno != ESRCH) warn_errno("kill(zegar)");

                    /* waitpid(): czekamy aż zegar się zakończy*/
                    while (waitpid(zegar_pid, NULL, 0) == -1 && errno == EINTR) {}

                    ewakuacja(msgid, stan);
                    break;
                }

                /* Sygnały 1/2: blokuj/odblokuj konkretny sektor*/
                if (cmd == 1 || cmd == 2) {
                    int s;
                    printf("Sektor (0-7): ");
                    fflush(stdout);

                    if (scanf("%d", &s) == 1 && s >= 0 && s < LICZBA_SEKTOROW) {
                        MsgSterujacy msg = {10 + s, cmd, s};

                        /* msgsnd(): wysyła polecenie sterowania do pracownika sektora*/
                        if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) {
                            if (!(errno == EIDRM || errno == EINVAL)) warn_errno("msgsnd(sterowanie)");
                        }
                    }
                }
            }
        }
    }

out:
    /* shmdt(): odłącza shm od procesu kierownika*/
    if (shmdt(stan) == -1) warn_errno("shmdt");

    /* Na wszelki wypadek dobijam zegar jeśli jeszcze żyje*/
    /* kill(): wysyła sygnał do procesu*/
    if (kill(zegar_pid, SIGTERM) == -1 && errno != ESRCH) warn_errno("kill(zegar)");

    /* waitpid(): sprząta proces potomny żeby nam się zombie nie zrobił*/
    while (waitpid(zegar_pid, NULL, 0) == -1 && errno == EINTR) {}
    return 0;
}
