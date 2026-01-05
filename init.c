#include "common.h"

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main() {
    printf("--- SETUP (STRICT MODE) ---\n");

    /* Tworze plik raportu*/
    FILE *rf = fopen("raport.txt", "w");
    if (rf) {
        fprintf(rf, "id typ sektor\n");
        fclose(rf);
    }

    /* shmget(): tworzy/pobiera segment pamięci współdzielonej*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), IPC_CREAT | 0600);
    if (shmid == -1) die_errno("shmget");

    /* shmat(): podłącza shm do przestrzeni adresowej procesu*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        warn_errno("shmat");
        /* shmctl(IPC_RMID): usuwa segment shm: sprzątanie po błędzie*/
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
        /* exit(): kończy proces kodem błędu. */
        exit(EXIT_FAILURE);
    }

    /* Inicjalizacja stanu symulacji w pamięci współdzielonej*/
    memset(stan, 0, sizeof(SharedState));
    stan->aktywne_kasy[0] = 1;
    stan->aktywne_kasy[1] = 1;
    stan->next_kibic_id = DYN_ID_START;
    stan->standard_sold_out = 0;
    stan->sprzedaz_zakonczona = 0;
    stan->cnt_weszlo = 0;
    stan->cnt_opiekun = 0;
    stan->cnt_kolega = 0;
    stan->cnt_agresja = 0;

    /* shmdt(): odłącza shm od procesu*/
    if (shmdt(stan) == -1) warn_errno("shmdt");

    /* semget(): tworzy/pobiera zestaw semaforów*/
    int n_sem = 3 + LICZBA_SEKTOROW;
    int semid = semget(KEY_SEM, n_sem, IPC_CREAT | 0600);
    if (semid == -1) {
        warn_errno("semget");
        /* shmctl(IPC_RMID): usuwa segment shm: sprzątanie po błędzie*/
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
        exit(EXIT_FAILURE);
    }

    /* semctl(SETVAL): ustawiam każdy semafor na 1*/
    union semun arg;
    arg.val = 1;
    for (int i = 0; i < n_sem; i++) {
        if (semctl(semid, i, SETVAL, arg) == -1) {
            warn_errno("semctl(SETVAL)");
            /* shmctl(IPC_RMID): usuwa segment shm: sprzątanie po błędzie*/
            if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
            /* semctl(IPC_RMID): usunięcie całego zestawu semaforów*/
            if (semctl(semid, 0, IPC_RMID) == -1) warn_errno("semctl(IPC_RMID)");
            exit(EXIT_FAILURE);
        }
    }

    /* msgget(): tworzy/pobiera kolejkę komunikatów*/
    int msgid = msgget(KEY_MSG, IPC_CREAT | 0600);
    if (msgid == -1) {
        warn_errno("msgget");
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
        if (semctl(semid, 0, IPC_RMID) == -1) warn_errno("semctl(IPC_RMID)");
        exit(EXIT_FAILURE);
    }

    printf("[OK] Zasoby utworzone.\n");
    return 0;
}
