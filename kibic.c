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

    int my_id = atoi(argv[1]);
    (void)my_id;
    (void)argv[2];

    srand(time(NULL) ^ (getpid() << 16));
    int druzyna = rand() % 2;

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
    if (b.sektor_id < 0 || b.sektor_id >= LICZBA_SEKTOROW) {
        shmdt(stan);
        exit(0);
    }

    int sektor = b.sektor_id;
    int sem_sektor = SEM_SEKTOR_START + sektor;

    while (1) {
        if (stan->ewakuacja_trwa) break;
        if (stan->blokada_sektora[sektor]) { usleep(200000); continue; }

        sem_op(semid, sem_sektor, -1);

        int wybrane = -1;
        for (int i = 0; i < 2; i++) {
            int n = stan->bramki[sektor][i].zajetosc;
            int d = stan->bramki[sektor][i].druzyna;
            if (n < MAX_NA_STANOWISKU) {
                if (n == 0 || d == druzyna) {
                    wybrane = i;
                    break;
                }
            }
        }

        if (wybrane != -1) {
            stan->bramki[sektor][wybrane].zajetosc++;
            stan->bramki[sektor][wybrane].druzyna = druzyna;

            printf("[SEKTOR %d|ST %d] Wchodzi %s. Stan: %d/3\n",
                   sektor, wybrane, (druzyna == 0 ? "GOSP" : "GOSC"),
                   stan->bramki[sektor][wybrane].zajetosc);
            fflush(stdout);

            sem_op(semid, sem_sektor, 1);

            usleep(300000);

            sem_op(semid, sem_sektor, -1);
            stan->bramki[sektor][wybrane].zajetosc--;
            sem_op(semid, sem_sektor, 1);
            break;
        }

        sem_op(semid, sem_sektor, 1);
        usleep(100000);
    }

    shmdt(stan);
    return 0;
}
