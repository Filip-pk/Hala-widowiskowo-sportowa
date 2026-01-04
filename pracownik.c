#include "common.h"

static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {(unsigned short)idx, (short)op, 0};
    while (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) _exit(0);
        die_errno("semop");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Użycie: %s <sektor_id>\n", argv[0]);
        exit(1);
    }

    int sektor = atoi(argv[1]);
    if (sektor < 0 || sektor >= LICZBA_SEKTOROW) {
        fprintf(stderr, "Błąd: sektor poza zakresem 0..%d\n", LICZBA_SEKTOROW - 1);
        exit(EXIT_FAILURE);
    }

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) die_errno("shmget");

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) die_errno("msgget");

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) die_errno("semget");

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    long my_type = 10 + sektor;

    while (1) {
        MsgSterujacy msg;
        if (msgrcv(msgid, &msg, sizeof(int) * 2, my_type, 0) == -1) {
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) break;
            warn_errno("msgrcv");
            break;
        }

        if (msg.typ_sygnalu == 1) {
            stan->blokada_sektora[sektor] = 1;
            printf("[TECH %d] Sygnał 1\n", sektor);
            fflush(stdout);
        } else if (msg.typ_sygnalu == 2) {
            stan->blokada_sektora[sektor] = 0;
            printf("[TECH %d] Sygnał 2\n", sektor);
            fflush(stdout);
        } else if (msg.typ_sygnalu == 3) {
            printf("[TECH %d] Sygnał 3\n", sektor);
            fflush(stdout);

            stan->blokada_sektora[sektor] = 1;

            int sem_sektora = SEM_SEKTOR_START + sektor;

            while (1) {
                int b0, b1, ob;

                sem_op(semid, sem_sektora, -1);
                b0 = stan->bramki[sektor][0].zajetosc;
                b1 = stan->bramki[sektor][1].zajetosc;
                sem_op(semid, sem_sektora, 1);

                sem_op(semid, SEM_SHM, -1);
                ob = stan->obecni_w_sektorze[sektor];
                sem_op(semid, SEM_SHM, 1);

                if (b0 == 0 && b1 == 0 && ob == 0) break;
                usleep(100000);
            }

            msg.mtype = 99;
            msg.sektor_id = sektor;
            if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) {
                if (!(errno == EIDRM || errno == EINVAL)) warn_errno("msgsnd(raport)");
            }

            printf("[TECH %d] Raport wysłany\n", sektor);
            fflush(stdout);
            break;
        }
    }

    if (shmdt(stan) == -1) warn_errno("shmdt");
    return 0;
}
