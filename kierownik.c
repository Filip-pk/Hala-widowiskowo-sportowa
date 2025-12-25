#include "common.h"
#include <sys/wait.h>

static void ewakuacja(int msgid, SharedState *stan) {
    stan->ewakuacja_trwa = 1;
    stan->status_meczu = 2;

    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        MsgSterujacy msg = {10 + i, 3, i};
        msgsnd(msgid, &msg, sizeof(int) * 2, 0);
    }

    int raporty = 0;
    while (raporty < LICZBA_SEKTOROW) {
        MsgSterujacy rap;
        if (msgrcv(msgid, &rap, sizeof(int) * 2, 99, 0) >= 0) {
            printf("[RAPORT] Sektor %d pusty\n", rap.sektor_id);
            fflush(stdout);
            raporty++;
        }
    }

    printf("[KIEROWNIK] Koniec\n");
    fflush(stdout);
}

int main() {
    setbuf(stdout, NULL);

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) exit(1);

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) exit(1);

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) exit(1);

    stan->status_meczu = 0;
    stan->czas_pozostaly = CZAS_PRZED_MECZEM;

    pid_t zegar_pid = fork();
    if (zegar_pid == 0) {
        time_t start = time(NULL);
        while (1) {
            int left = CZAS_PRZED_MECZEM - (int)(time(NULL) - start);
            if (left <= 0) break;
            stan->czas_pozostaly = left;
            sleep(1);
        }

        stan->status_meczu = 1;
        stan->czas_pozostaly = CZAS_MECZU;

        start = time(NULL);
        while (1) {
            int left = CZAS_MECZU - (int)(time(NULL) - start);
            if (left <= 0) break;
            stan->czas_pozostaly = left;
            sleep(1);
        }

        stan->status_meczu = 2;
        stan->czas_pozostaly = 0;
        exit(0);
    }

    printf("Komendy: 1-stop sektora, 2-start sektora, 3-ewakuacja\n");
    fflush(stdout);

    while (1) {
        if (waitpid(zegar_pid, NULL, WNOHANG) > 0) {
            ewakuacja(msgid, stan);
            break;
        }

        int cmd;
        if (scanf("%d", &cmd) == 1) {
            if (cmd == 3) {
                kill(zegar_pid, SIGTERM);
                waitpid(zegar_pid, NULL, 0);
                ewakuacja(msgid, stan);
                break;
            }
            if (cmd == 1 || cmd == 2) {
                int s;
                printf("Sektor (0-7): ");
                fflush(stdout);
                if (scanf("%d", &s) == 1 && s >= 0 && s < LICZBA_SEKTOROW) {
                    MsgSterujacy msg = {10 + s, cmd, s};
                    msgsnd(msgid, &msg, sizeof(int) * 2, 0);
                }
            }
        }
        usleep(100000);
    }

    shmdt(stan);
    return 0;
}
