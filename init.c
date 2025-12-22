#include "common.h"

int main() {
    printf("--- SETUP ---\n");

    int shmid = shmget(KEY_SHM, sizeof(SharedState), IPC_CREAT | 0600);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    memset(stan, 0, sizeof(SharedState));
    shmdt(stan);

    printf("[OK] SHM utworzone.\n");
    return 0;
}
