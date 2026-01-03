#include "common.h"

int main() {
    int shmid = shmget(KEY_SHM, 0, 0600);
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) perror("shmctl");
    } else if (errno != ENOENT) perror("shmget");

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) perror("semctl");
    } else if (errno != ENOENT) perror("semget");

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid != -1) {
        if (msgctl(msgid, IPC_RMID, NULL) == -1) perror("msgctl");
    } else if (errno != ENOENT) perror("msgget");

    printf("[OK] Zasoby usunięte.\n");
    return 0;
}
