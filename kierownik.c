#include "common.h"
#include <sys/wait.h>

int main() {
    setbuf(stdout, NULL);

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) { perror("msgget"); exit(1); }

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) { perror("shmget"); exit(1); }

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) { perror("shmat"); exit(1); }

    stan->status_meczu = 0;
    stan->czas_pozostaly = CZAS_PRZED_MECZEM;

    pid_t zegar = fork();
    if (zegar == 0) {
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

    printf("--- KIEROWNIK URUCHOMIONY ---\n");
    printf("Komendy: 1-Stop sektora, 2-Start sektora\n");
    fflush(stdout);

    while (1) {
        if (waitpid(zegar, NULL, WNOHANG) > 0) break;

        int cmd, s;
        if (scanf("%d", &cmd) == 1) {
            if (cmd == 1 || cmd == 2) {
                printf("Podaj nr sektora (0-7): ");
                fflush(stdout);
                if (scanf("%d", &s) == 1 && s >= 0 && s < LICZBA_SEKTOROW) {
                    MsgSterujacy msg = {10 + s, cmd, s};
                    if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) perror("msgsnd");
                }
            }
        }
        usleep(100000);
    }

    shmdt(stan);
    return 0;
}
