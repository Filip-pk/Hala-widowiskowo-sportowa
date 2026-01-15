#include "common.h"

/*
 * ==================
 * CLEAN: sprzątanie
 * ==================
 * ./clean usuwa zasoby po kluczach KEY_* i jest wołany przez main na końcu.
 */

int main() {
    /* shmget(): próba znalezienia istniejącego segmentu shm*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid != -1) {
        /* shmctl(IPC_RMID): usuwa segment pamięci współdzielonej*/
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
    } else if (errno != ENOENT) warn_errno("shmget");

    /* semget(): próba znalezienia istniejącego zestawu semaforów*/
    int semid = semget(KEY_SEM, N_SEM, 0600);
    if (semid != -1) {
        /* semctl(IPC_RMID): usuwa cały zestaw semaforów*/
        if (semctl(semid, 0, IPC_RMID) == -1) warn_errno("semctl(IPC_RMID)");
    } else if (errno != ENOENT) warn_errno("semget");

    /* msgget(): próba znalezienia istniejącej kolejki komunikatów*/
    int msgid = msgget(KEY_MSG, 0600);
    if (msgid != -1) {
        /* msgctl(IPC_RMID): usuwa kolejkę komunikatów*/
        if (msgctl(msgid, IPC_RMID, NULL) == -1) warn_errno("msgctl(IPC_RMID)");
    } else if (errno != ENOENT) warn_errno("msgget");

    /* Druga kolejka: bilety (kasjer -> kibic) */
    int msgid_ticket = msgget(KEY_MSG_TICKET, 0600);
    if (msgid_ticket != -1) {
        if (msgctl(msgid_ticket, IPC_RMID, NULL) == -1) warn_errno("msgctl(IPC_RMID ticket)");
    } else if (errno != ENOENT) warn_errno("msgget(ticket)");

    printf("[OK] Zasoby usunięte.\n");
    return 0;
}
