#include "common.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Użycie: %s <sektor_id>\n", argv[0]);
        exit(1);
    }

    int sektor = atoi(argv[1]);

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) { perror("shmget"); exit(1); }

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) { perror("msgget"); exit(1); }

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) { perror("shmat"); exit(1); }

    long my_type = 10 + sektor;

    while (1) {
        MsgSterujacy msg;
        if (msgrcv(msgid, &msg, sizeof(int) * 2, my_type, 0) == -1) {
            if (errno == EINTR) continue;
            perror("msgrcv");
            break;
        }

        if (msg.typ_sygnalu == 1) {
            stan->blokada_sektora[sektor] = 1;
            printf("[TECH %d] Sygnał 1: Blokada wejścia.\n", sektor);
            fflush(stdout);
        } else if (msg.typ_sygnalu == 2) {
            stan->blokada_sektora[sektor] = 0;
            printf("[TECH %d] Sygnał 2: Wznowienie wejścia.\n", sektor);
            fflush(stdout);
        }
    }

    shmdt(stan);
    return 0;
}
