#include "common.h"

#include <sys/wait.h>

static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {idx, op, 0};
    if (semop(semid, &sb, 1) == -1) exit(0);
}

static void send_ticket(int msgid, int kibic_id, int sektor) {
    MsgBilet msg;
    msg.mtype = MSGTYPE_TICKET_BASE + kibic_id;
    msg.sektor_id = sektor;

    if (msgsnd(msgid, &msg, sizeof(int), 0) == -1) {
        if (!(errno == EIDRM || errno == EINVAL)) perror("msgsnd");
    }
}

static int standard_sold_out(SharedState *stan, int limit_sektor) {
    for (int s = 0; s < LICZBA_SEKTOROW; s++) {
        if (stan->sprzedane_bilety[s] < limit_sektor) return 0;
    }
    return 1;
}

static int all_sold_out(SharedState *stan, int limit_sektor, int limit_vip) {
    if (!standard_sold_out(stan, limit_sektor)) return 0;
    if (stan->sprzedane_bilety[SEKTOR_VIP] < limit_vip) return 0;
    return 1;
}

static void spawn_friend_kibic(int friend_id) {
    pid_t pid = fork();
    if (pid == 0) {
        char idbuf[32];
        sprintf(idbuf, "%d", friend_id);
        execl("./kibic", "kibic", idbuf, "0", "1", NULL);
        _exit(1);
    }
}

static void cancel_queue_type(int msgid, long req_type) {
    MsgKolejka req;
    while (1) {
        ssize_t r = msgrcv(msgid, &req, sizeof(int), req_type, IPC_NOWAIT);
        if (r >= 0) {
            send_ticket(msgid, req.kibic_id, -1);
            continue;
        }
        if (errno == ENOMSG) break;
        if (errno == EINTR) continue;
        break;
    }
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    if (argc != 2) {
        fprintf(stderr, "Użycie: %s <id>\n", argv[0]);
        exit(1);
    }

    int id = atoi(argv[1]);
    srand(time(NULL) + id);

    signal(SIGCHLD, SIG_IGN);

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) exit(1);

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) exit(1);

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) exit(1);

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) exit(1);

    int limit_sektor = K / 8;
    int limit_vip = (int)(K * 0.003);
    if (limit_vip < 1) limit_vip = 1;
    int k_10 = K / 10;

    while (1) {
        if (stan->ewakuacja_trwa) break;
        if (stan->sprzedaz_zakonczona) break;

        if (stan->aktywne_kasy[id] == 0) {
            usleep(100000);
            continue;
        }

        int klient_typ = 0;
        int kibic_id = -1;

        sem_op(semid, SEM_KASY, -1);

        int q_vip = stan->kolejka_vip;
        int q_std = stan->standard_sold_out ? 0 : stan->kolejka_zwykla;
        int total_queue = q_vip + q_std;

        int N = 0;
        for (int i = 0; i < LICZBA_KAS; i++) if (stan->aktywne_kasy[i]) N++;

        int prog_zamykania = k_10 * (N - 1);
        if (N > 2 && total_queue < prog_zamykania) {
            if (id > 1) {
                stan->aktywne_kasy[id] = 0;
                printf(CLR_RED "[KASA %d] ZAMYKAM SIĘ (kolejka=%d, próg=%d)" CLR_RESET "\n",
                       id, total_queue, prog_zamykania);
                fflush(stdout);
                sem_op(semid, SEM_KASY, 1);
                continue;
            }
        }

        int wymagane_kasy = (total_queue / k_10) + 1;
        if (wymagane_kasy > N && N < LICZBA_KAS) {
            for (int i = 0; i < LICZBA_KAS; i++) {
                if (stan->aktywne_kasy[i] == 0) {
                    stan->aktywne_kasy[i] = 1;
                    printf(CLR_GREEN "[SYSTEM] OTWIERAM KASĘ %d (kolejka=%d, aktywne=%d->%d)" CLR_RESET "\n",
                           i, total_queue, N, N + 1);
                    fflush(stdout);
                    break;
                }
            }
        }

        sem_op(semid, SEM_KASY, 1);

        MsgKolejka req;

        if (q_vip > 0) {
            if (msgrcv(msgid, &req, sizeof(int), MSGTYPE_VIP_REQ, IPC_NOWAIT) != -1) {
                klient_typ = 1;
                kibic_id = req.kibic_id;
                sem_op(semid, SEM_KASY, -1);
                if (stan->kolejka_vip > 0) stan->kolejka_vip--;
                sem_op(semid, SEM_KASY, 1);
            }
        }

        if (!klient_typ && !stan->standard_sold_out && q_std > 0) {
            if (msgrcv(msgid, &req, sizeof(int), MSGTYPE_STD_REQ, IPC_NOWAIT) != -1) {
                klient_typ = 2;
                kibic_id = req.kibic_id;
                sem_op(semid, SEM_KASY, -1);
                if (stan->kolejka_zwykla > 0) stan->kolejka_zwykla--;
                sem_op(semid, SEM_KASY, 1);
            }
        }

        if (!klient_typ) {
            usleep(50000);
            continue;
        }

        if (stan->ewakuacja_trwa) break;
        if (stan->sprzedaz_zakonczona) break;

        usleep(100000);

        if (klient_typ == 1) {
            int sektor = -1;
            int set_all = 0;

            sem_op(semid, SEM_SHM, -1);
            if (stan->sprzedane_bilety[SEKTOR_VIP] < limit_vip) {
                stan->sprzedane_bilety[SEKTOR_VIP]++;
                sektor = SEKTOR_VIP;
            }
            if (all_sold_out(stan, limit_sektor, limit_vip)) {
                if (!stan->sprzedaz_zakonczona) set_all = 1;
                stan->sprzedaz_zakonczona = 1;
            }
            sem_op(semid, SEM_SHM, 1);

            if (set_all) {
                printf(CLR_YELLOW "[SYSTEM] WSZYSTKIE BILETY WYPRZEDANE - koniec sprzedaży." CLR_RESET "\n");
                fflush(stdout);
            }

            send_ticket(msgid, kibic_id, sektor);

            if (stan->sprzedaz_zakonczona) {
                sem_op(semid, SEM_KASY, -1);
                stan->kolejka_vip = 0;
                stan->kolejka_zwykla = 0;
                for (int i = 0; i < LICZBA_KAS; i++) stan->aktywne_kasy[i] = 0;
                sem_op(semid, SEM_KASY, 1);

                cancel_queue_type(msgid, MSGTYPE_VIP_REQ);
                cancel_queue_type(msgid, MSGTYPE_STD_REQ);
                break;
            }
            continue;
        }

        int sektor = -1;
        int ile_sprzedane = 1;
        int friend_id = -1;
        int set_standard_now = 0;

        int start = rand() % LICZBA_SEKTOROW;
        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            int s = (start + i) % LICZBA_SEKTOROW;

            sem_op(semid, SEM_SHM, -1);
            if (stan->sprzedane_bilety[s] < limit_sektor) {
                int chciane = (rand() % 2) + 1;
                int ile = 1;

                if (chciane == 2 && stan->sprzedane_bilety[s] + 2 <= limit_sektor) {
                    ile = 2;
                }

                stan->sprzedane_bilety[s] += ile;
                sektor = s;
                ile_sprzedane = ile;

                if (ile == 2) {
                    friend_id = stan->next_kibic_id++;
                }

                if (!stan->standard_sold_out && standard_sold_out(stan, limit_sektor)) {
                    stan->standard_sold_out = 1;
                    set_standard_now = 1;
                }
                sem_op(semid, SEM_SHM, 1);

                if (set_standard_now) {
                    printf(CLR_YELLOW "[SYSTEM] STANDARD SOLD OUT - kończymy obsługę zwykłych kas." CLR_RESET "\n");
                    fflush(stdout);

                    sem_op(semid, SEM_KASY, -1);
                    stan->kolejka_zwykla = 0;
                    sem_op(semid, SEM_KASY, 1);

                    cancel_queue_type(msgid, MSGTYPE_STD_REQ);
                }

                if (ile == 2) {
                    printf("[KASA %d] Sprzedano 2 bilety do sektora %d (drugi dla kolegi %d).\n",
                           id, s, friend_id);
                } else {
                    printf("[KASA %d] Sprzedano 1 bilet do sektora %d.\n", id, s);
                }
                fflush(stdout);

                break;
            }
            sem_op(semid, SEM_SHM, 1);
        }

        if (sektor == -1) {
            int set_standard = 0;
            int set_all = 0;

            sem_op(semid, SEM_SHM, -1);
            if (standard_sold_out(stan, limit_sektor)) {
                if (!stan->standard_sold_out) set_standard = 1;
                stan->standard_sold_out = 1;
            }
            if (all_sold_out(stan, limit_sektor, limit_vip)) {
                stan->sprzedaz_zakonczona = 1;
                set_all = 1;
            }
            sem_op(semid, SEM_SHM, 1);

            if (set_standard) {
                printf(CLR_YELLOW "[SYSTEM] STANDARD SOLD OUT - kończymy obsługę zwykłych kas." CLR_RESET "\n");
                fflush(stdout);
            }
            if (set_all) {
                printf(CLR_YELLOW "[SYSTEM] WSZYSTKIE BILETY WYPRZEDANE - koniec sprzedaży." CLR_RESET "\n");
                fflush(stdout);
            }

            send_ticket(msgid, kibic_id, -1);

            if (set_standard) {
                sem_op(semid, SEM_KASY, -1);
                stan->kolejka_zwykla = 0;
                sem_op(semid, SEM_KASY, 1);
                cancel_queue_type(msgid, MSGTYPE_STD_REQ);
            }

            if (set_all) {
                sem_op(semid, SEM_KASY, -1);
                stan->kolejka_vip = 0;
                stan->kolejka_zwykla = 0;
                for (int i = 0; i < LICZBA_KAS; i++) stan->aktywne_kasy[i] = 0;
                sem_op(semid, SEM_KASY, 1);

                cancel_queue_type(msgid, MSGTYPE_VIP_REQ);
                cancel_queue_type(msgid, MSGTYPE_STD_REQ);
                break;
            }

            stan->aktywne_kasy[id] = 0;
            printf(CLR_RED "[KASA %d] SOLD OUT - ZAMYKAM" CLR_RESET "\n", id);
            fflush(stdout);
            continue;
        }

        if (friend_id != -1 && ile_sprzedane == 2) {
            spawn_friend_kibic(friend_id);
        }

        send_ticket(msgid, kibic_id, sektor);
        if (friend_id != -1 && ile_sprzedane == 2) {
            send_ticket(msgid, friend_id, sektor);
        }
    }

    shmdt(stan);
    return 0;
}
