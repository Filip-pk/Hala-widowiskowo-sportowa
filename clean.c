#include "common.h"

int main() {
    int shmid = shmget(KEY_SHM, 0, 0600);
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
    } else if (errno != ENOENT) warn_errno("shmget");

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) warn_errno("semctl(IPC_RMID)");
    } else if (errno != ENOENT) warn_errno("semget");

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid != -1) {
        if (msgctl(msgid, IPC_RMID, NULL) == -1) warn_errno("msgctl(IPC_RMID)");
    } else if (errno != ENOENT) warn_errno("msgget");

    printf("[OK] Zasoby usunięte.\n");
    return 0;
}
