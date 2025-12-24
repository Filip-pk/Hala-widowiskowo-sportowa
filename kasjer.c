#include "common.h"

static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {idx, op, 0};
    if (semop(semid, &sb, 1) == -1) exit(0);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    if (argc != 2) {
        fprintf(stderr, "Użycie: %s <id>\n", argv[0]);
        exit(1);
    }

    int id = atoi(argv[1]);
    srand(time(NULL) + id);

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) exit(1);

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) exit(1);

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) exit(1);

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) exit(1);

    int limit_sektor = K / 8;

    while (1) {
        if (stan->ewakuacja_trwa) break;
        if (stan->aktywne_kasy[id] == 0) { usleep(100000); continue; }

        int mam_klienta = 0;

        sem_op(semid, SEM_KASY, -1);
        if (stan->kolejka_zwykla > 0) {
            stan->kolejka_zwykla--;
            mam_klienta = 1;
        }
        sem_op(semid, SEM_KASY, 1);

        if (!mam_klienta) { usleep(50000); continue; }

        usleep(100000);

        int sektor = -1;

        sem_op(semid, SEM_SHM, -1);
        int start = rand() % LICZBA_SEKTOROW;
        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            int s = (start + i) % LICZBA_SEKTOROW;
            if (stan->sprzedane_bilety[s] < limit_sektor) {
                int ile = (rand() % 2) + 1;
                if (stan->sprzedane_bilety[s] + ile <= limit_sektor) stan->sprzedane_bilety[s] += ile;
                else stan->sprzedane_bilety[s]++;
                sektor = s;
                break;
            }
        }
        sem_op(semid, SEM_SHM, 1);

        if (sektor == -1) {
            stan->aktywne_kasy[id] = 0;
            printf("[KASA %d] SOLD OUT\n", id);
            fflush(stdout);
        } else {
            printf("[KASA %d] Sprzedano bilet do sektora %d\n", id, sektor);
            fflush(stdout);
        }

        MsgBilet msg;
        msg.mtype = 999;
        msg.sektor_id = sektor;
        msgsnd(msgid, &msg, sizeof(int), 0);
    }

    shmdt(stan);
    return 0;
}
