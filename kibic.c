#include "common.h"

static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {idx, op, 0};
    if (semop(semid, &sb, 1) == -1) exit(0);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if (argc != 3) {
        fprintf(stderr, "Użycie: %s <id> <vip>\n", argv[0]);
        exit(1);
    }

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) exit(0);

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) exit(0);

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) exit(0);

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) exit(0);

    sem_op(semid, SEM_KASY, -1);
    stan->kolejka_zwykla++;
    sem_op(semid, SEM_KASY, 1);

    MsgBilet b;
    if (msgrcv(msgid, &b, sizeof(int), 999, 0) == -1) {
        shmdt(stan);
        exit(0);
    }

    shmdt(stan);
    return 0;
}
