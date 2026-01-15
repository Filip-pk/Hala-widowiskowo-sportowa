#include "common.h"

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

/*
 * ==========================
 * PRACOWNIK SEKTORA
 * ==========================
 * Jeden pracownik = jeden sektor (0..7).
 * Pracownik jest ramieniem wykonawczym kierownika:
 *  - odbiera MsgSterujacy z kolejki msg o typie mtype = 10 + sektor,
 *  - wykonuje komendy:
 *      1 -> blokada_sektora[sektor]=1
 *      2 -> blokada_sektora[sektor]=0
 *      3 -> ewakuacja sektora i raport do kierownika (mtype=99)
 *
 * Blokada jest w shm
 *  - kibice mogą ją odczytywać bezpośrednio (bez dodatkowej kolejki),
 *  - pracownik tylko ustawia flagę, a kibice reagują w swojej pętli wejścia.
 */


static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {(unsigned short)idx, (short)op, 0};
    while (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) _exit(0);
        die_errno("semop");
    }
}

int main(int argc, char *argv[]) {
    /* Pracownik odpowiada za JEDEN sektor i reaguje na komendy kierownika*/
    if (argc != 2) {
        fprintf(stderr, "Użycie: %s <sektor_id>\n", argv[0]);
        /* exit(): kończy proces kodem błędu*/
        exit(1);
    }

    int sektor = atoi(argv[1]);
    if (sektor < 0 || sektor >= LICZBA_SEKTOROW) {
        fprintf(stderr, "Błąd: sektor poza zakresem 0..%d\n", LICZBA_SEKTOROW - 1);
        exit(EXIT_FAILURE);
    }

    /* shmget(): pobiera segment pamięci współdzielonej*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) die_errno("shmget");

    /* msgget(): pobiera kolejkę komunikatów*/
    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) die_errno("msgget");

    /* semget(): pobiera zestaw semaforów*/
    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) die_errno("semget");

    /* shmat(): mapuje shm do pamięci procesu*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    long my_type = 10 + sektor;

    while (1) {
        MsgSterujacy msg;

/*
 * Odbieramy komendy dla tego konkretnego sektora:
 *  - my_type = 10 + sektor
 * dzięki temu jeden kanał msg obsługuje wszystkie sektory,
 * ale każdy pracownik filtruje tylko swoje wiadomości.
 */

        /* msgrcv(): odbiera polecenie z kolejki */
        if (msgrcv(msgid, &msg, sizeof(int) * 2, my_type, 0) == -1) {
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) break; /* kolejka skasowana -> kończymy */
            warn_errno("msgrcv");
            break;
        }

        /*
         * typ_sygnalu:
         *  1 -> blokuj sektor
         *  2 -> odblokuj sektor
         *  3 -> ewakuacja
         */
        if (msg.typ_sygnalu == 1) {
            stan->blokada_sektora[sektor] = 1;
            /* semafor-zdarzenie: 1 = zablokowany */
            union semun a; a.val = 1;
            if (semctl(semid, SEM_SEKTOR_BLOCK_START + sektor, SETVAL, a) == -1) {
                if (errno == EIDRM || errno == EINVAL) break;
                warn_errno("semctl");
            }
            printf("[TECH %d] Sygnał 1 (BLOKADA)\n", sektor);
            fflush(stdout);

        } else if (msg.typ_sygnalu == 2) {
            stan->blokada_sektora[sektor] = 0;
            /* semafor-zdarzenie: 0 = odblokowany */
            union semun a; a.val = 0;
            if (semctl(semid, SEM_SEKTOR_BLOCK_START + sektor, SETVAL, a) == -1) {
                if (errno == EIDRM || errno == EINVAL) break;
                warn_errno("semctl");
            }
            printf("[TECH %d] Sygnał 2 (ODBLOKOWANIE)\n", sektor);
            fflush(stdout);

        } else if (msg.typ_sygnalu == 3) {
            printf("[TECH %d] Sygnał 3 (EWAKUACJA)\n", sektor);
            fflush(stdout);

/*
 * W ewakuacji warunek „sektor pusty” jest dwuetapowy:
 *  1) bramki puste (pod semaforem sektora) -> nikt nie jest w przejściu,
 *  2) obecni_w_sektorze==0 (pod SEM_SHM)  -> nikt nie siedzi w sektorze.
 *
 * Dopiero wtedy odsyłamy raport do kierownika (mtype=99),
 * a kierownik kończy symulację dopiero po zebraniu 8 raportów.
 */

            /* W ewakuacji blokujemy sektor w shm, ale semafor zdarzenia zostawiamy OTWARTY (0),
               żeby nikt nie utknął na czekaniu na odblokowanie. */
            stan->blokada_sektora[sektor] = 1;
            union semun a; a.val = 0;
            if (semctl(semid, SEM_SEKTOR_BLOCK_START + sektor, SETVAL, a) == -1) {
                if (errno == EIDRM || errno == EINVAL) break;
                warn_errno("semctl");
            }

            int sem_sektora = SEM_SEKTOR_START + sektor;

            while (1) {
                int b0, b1, ob;

                /* Bramki sektora chronione semaforem sektora*/
                sem_op(semid, sem_sektora, -1);
                b0 = stan->bramki[sektor][0].zajetosc;
                b1 = stan->bramki[sektor][1].zajetosc;
                sem_op(semid, sem_sektora, 1);

                /* Licznik obecnych w sektorze*/
                sem_op(semid, SEM_SHM, -1);
                ob = stan->obecni_w_sektorze[sektor];
                sem_op(semid, SEM_SHM, 1);

                /* Dopiero gdy bramki puste i nikt nie siedzi w sektorze -> sektor ewakuowany*/
                if (b0 == 0 && b1 == 0 && ob == 0) break;
                usleep(10000);
            }

            /* Raport do kierownika: sektor pusty*/
            msg.mtype = 99;
            msg.sektor_id = sektor;

            /* msgsnd(): wysyła raport do kolejki komunikatów*/
            if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) {
                if (!(errno == EIDRM || errno == EINVAL)) warn_errno("msgsnd(raport)");
            }

            printf("[TECH %d] Raport wysłany\n", sektor);
            fflush(stdout);
            break;
        }
    }

    /* shmdt(): odłącza shm od procesu pracownika*/
    if (shmdt(stan) == -1) warn_errno("shmdt");
    return 0;
}
