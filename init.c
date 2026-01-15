#include "common.h"

/*
 * ======================
 * SETUP: start symulacji
 * ======================
 * ./setup tworzy wszystkie zasoby IPC (System V):
 *  - pamięć współdzieloną (shm): SharedState,
 *  - semafory (sem): mutexy do shm, kas i sektorów,
 *  - kolejkę komunikatów (msg): bilety i komendy.
 *
 * Dodatkowo resetuje raport.txt i inicjalizuje stan:
 *  - startujemy z 2 aktywnymi kasami (0 i 1),
 *  - next_kibic_id ustawiamy na DYN_ID_START (ID dla „kolegów” z 2 biletów).
 *
 * Ten plik jest odpalany przed main:
 *   ./setup
 *   ./main
 */

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main() {
    printf("--- SETUP ---\n");

/*
 * raport.txt jest wspólnym artefaktem wyjściowym symulacji.
 * Resetujemy go na starcie i wpisujemy nagłówek.
 * Potem kibice dopisują do niego w O_APPEND z flock().
 */

    /* Tworze plik raportu*/
    FILE *rf = fopen("raport.txt", "w");
    if (!rf) {
        /* fopen() ustawia errno -> wymaganie: perror()+errno */
        warn_errno("fopen(raport.txt)");
    } else {
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

/*
 * Semafory:
 *  - SEM_SHM (0): wspólne liczniki/flag w SharedState,
 *  - SEM_KASY (1): operacje na kolejkach i aktywności kas,
 *  - SEM_SEKTOR_START: po jednym na sektor (bramki + agresor),
 *  - SEM_KIEROWNIK: wybór master-kierownika,
 *  - SEM_EWAKUACJA: zdarzenie ewakuacji,
 *  - SEM_SEKTOR_BLOCK_START: semafory blokad sektorow.
 */

    /* semget(): tworzy zestaw semaforów*/
    int n_sem = N_SEM;
    int semid = semget(KEY_SEM, n_sem, IPC_CREAT | 0600);
    if (semid == -1) {
        warn_errno("semget");
        /* shmctl(IPC_RMID): usuwa segment shm: sprzątanie po błędzie*/
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
        exit(EXIT_FAILURE);
    }

    /* semctl(SETVAL): inicjalizacja semaforów
     *  - mutexy startują od 1
     *  - SEM_EWAKUACJA startuje od 1 (czekamy aż spadnie do 0)
     *  - SEM_SEKTOR_BLOCK_START startują od 0 (sektory otwarte)
     */
    union semun arg;
    for (int i = 0; i < n_sem; i++) {
        int v = 1;
        if (i >= SEM_SEKTOR_BLOCK_START && i < SEM_SEKTOR_BLOCK_START + LICZBA_SEKTOROW) {
            v = 0; /* sektory otwarte */
        }
        /* SEM_EWAKUACJA = 1 (domyślnie) */
        arg.val = v;
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
        warn_errno("msgget(req)");
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
        if (semctl(semid, 0, IPC_RMID) == -1) warn_errno("semctl(IPC_RMID)");
        exit(EXIT_FAILURE);
    }

    /* Druga kolejka: bilety (odpowiedzi kasjer -> kibic).
     * Dzięki temu kasjer nie zablokuje się na msgsnd(), gdy kolejka żądań
     * jest zapchana.
     */
    int msgid_ticket = msgget(KEY_MSG_TICKET, IPC_CREAT | 0600);
    if (msgid_ticket == -1) {
        warn_errno("msgget(ticket)");
        if (msgctl(msgid, IPC_RMID, NULL) == -1) warn_errno("msgctl(IPC_RMID)");
        if (shmctl(shmid, IPC_RMID, NULL) == -1) warn_errno("shmctl(IPC_RMID)");
        if (semctl(semid, 0, IPC_RMID) == -1) warn_errno("semctl(IPC_RMID)");
        exit(EXIT_FAILURE);
    }

    printf("[OK] Zasoby utworzone.\n");
    return 0;
}
