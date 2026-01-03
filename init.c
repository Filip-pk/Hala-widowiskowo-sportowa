#include "common.h"

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main() {
    printf("--- SETUP (STRICT MODE) ---\n");

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
    stan->aktywne_kasy[0] = 1;
    stan->aktywne_kasy[1] = 1;

    stan->next_kibic_id = DYN_ID_START;

    shmdt(stan);

    int n_sem = 2 + LICZBA_SEKTOROW;
    int semid = semget(KEY_SEM, n_sem, IPC_CREAT | 0600);
    if (semid == -1) {
        perror("semget");
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    union semun arg;
    arg.val = 1;
    for (int i = 0; i < n_sem; i++) {
        if (semctl(semid, i, SETVAL, arg) == -1) {
            perror("semctl");
            shmctl(shmid, IPC_RMID, NULL);
            semctl(semid, 0, IPC_RMID);
            exit(1);
        }
    }

    int msgid = msgget(KEY_MSG, IPC_CREAT | 0600);
    if (msgid == -1) {
        perror("msgget");
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
        exit(1);
    }

    printf("[OK] Zasoby utworzone.\n");
    return 0;
}
