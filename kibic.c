#include "common.h"

#include <fcntl.h>
#include <sys/file.h>

static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {(unsigned short)idx, (short)op, 0};
    while (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) _exit(0);
        die_errno("semop");
    }
}

static void obecni_inc(SharedState *stan, int semid, int sektor) {
    sem_op(semid, SEM_SHM, -1);
    stan->obecni_w_sektorze[sektor]++;
    sem_op(semid, SEM_SHM, 1);
}

static void obecni_dec(SharedState *stan, int semid, int sektor) {
    sem_op(semid, SEM_SHM, -1);
    if (stan->obecni_w_sektorze[sektor] > 0) stan->obecni_w_sektorze[sektor]--;
    sem_op(semid, SEM_SHM, 1);
}

static const char* team_color(int druzyna) {
    return (druzyna == 0) ? CLR_DBLUE : CLR_PURPLE;
}

static const char* team_name(int druzyna) {
    return (druzyna == 0) ? "GOSP" : "GOSC";
}

static void append_report(int kibic_id, int wiek, int sektor) {
    const char *typ = (sektor == SEKTOR_VIP) ? "vip" : ((wiek < 15) ? "opiekun" : "zwykly");

    int fd = open("raport.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) { warn_errno("open(raport.txt)"); return; }

    if (flock(fd, LOCK_EX) == -1) {
        warn_errno("flock(LOCK_EX)");
        if (close(fd) == -1) warn_errno("close(raport.txt)");
        return;
    }

    if (dprintf(fd, "%d %s %d\n", kibic_id, typ, sektor) < 0) {
        warn_errno("dprintf(raport.txt)");
    }

    if (flock(fd, LOCK_UN) == -1) warn_errno("flock(LOCK_UN)");
    if (close(fd) == -1) warn_errno("close(raport.txt)");
}

static void bump_entered(SharedState *stan, int semid, int wiek, int is_kolega) {
    sem_op(semid, SEM_SHM, -1);
    stan->cnt_weszlo++;
    if (wiek < 15) stan->cnt_opiekun++;
    if (is_kolega) stan->cnt_kolega++;
    sem_op(semid, SEM_SHM, 1);
}

static void bump_agresja(SharedState *stan, int semid) {
    sem_op(semid, SEM_SHM, -1);
    stan->cnt_agresja++;
    sem_op(semid, SEM_SHM, 1);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Użycie: %s <id> <vip> [ma_juz_bilet]\n", argv[0]);
        exit(1);
    }

    int my_id = atoi(argv[1]);
    int is_vip = atoi(argv[2]);
    int ma_juz_bilet = (argc == 4) ? atoi(argv[3]) : 0;

    srand(time(NULL) ^ (getpid() << 16));
    int wiek = 10 + rand() % 60;
    int druzyna = rand() % 2;

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) {
        warn_errno("shmget");
        exit(EXIT_FAILURE);
    }

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) {
        warn_errno("semget");
        exit(EXIT_FAILURE);
    }

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) {
        warn_errno("msgget");
        exit(EXIT_FAILURE);
    }

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    if (wiek < 15 && !is_vip) usleep(1000);
    if (stan->ewakuacja_trwa) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

    if (!ma_juz_bilet && !is_vip && stan->standard_sold_out) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

    if (!ma_juz_bilet && stan->sprzedaz_zakonczona) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

    if (!ma_juz_bilet) {
        sem_op(semid, SEM_KASY, -1);
        if (is_vip) stan->kolejka_vip++;
        else stan->kolejka_zwykla++;
        sem_op(semid, SEM_KASY, 1);

        MsgKolejka req;
        req.mtype = is_vip ? MSGTYPE_VIP_REQ : MSGTYPE_STD_REQ;
        req.kibic_id = my_id;

        while (msgsnd(msgid, &req, sizeof(int), 0) == -1) {
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) {
                if (shmdt(stan) == -1) warn_errno("shmdt");
                exit(0);
            }
            warn_errno("msgsnd(kolejka)");
            if (shmdt(stan) == -1) warn_errno("shmdt");
            exit(EXIT_FAILURE);
        }
    }

    MsgBilet bilet;
    while (1) {
        if (stan->ewakuacja_trwa) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }
        if (!ma_juz_bilet && stan->sprzedaz_zakonczona) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

        long my_ticket_type = MSGTYPE_TICKET_BASE + my_id;
        ssize_t r = msgrcv(msgid, &bilet, sizeof(int), my_ticket_type, IPC_NOWAIT);
        if (r >= 0) break;

        if (errno == ENOMSG) {
            if (!ma_juz_bilet && !is_vip && stan->standard_sold_out) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }
            usleep(50000);
            continue;
        }
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

        warn_errno("msgrcv(ticket)");
        if (shmdt(stan) == -1) warn_errno("shmdt");
        exit(EXIT_FAILURE);
    }

    if (bilet.sektor_id == -1) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

    int sektor = bilet.sektor_id;

    append_report(my_id, wiek, sektor);

    const int is_kolega = (my_id >= DYN_ID_START);

    if (sektor == SEKTOR_VIP) {
        bump_entered(stan, semid, wiek, is_kolega);
        printf(CLR_YELLOW "[VIP %d] WEJŚCIE VIP" CLR_RESET "\n", my_id);
        fflush(stdout);
        obecni_inc(stan, semid, SEKTOR_VIP);
        while (!stan->ewakuacja_trwa) usleep(200000);
        obecni_dec(stan, semid, SEKTOR_VIP);
        if (shmdt(stan) == -1) warn_errno("shmdt");
        exit(0);
    }

    int sem_sektora = SEM_SEKTOR_START + sektor;
    int cierpliwosc = 0;
    int wszedl_do_sektora = 0;

    while (1) {
        if (stan->ewakuacja_trwa) break;
        if (stan->blokada_sektora[sektor]) { usleep(200000); continue; }

        sem_op(semid, sem_sektora, -1);

        int wybrane = -1;
        int powod = 0;

        for (int i = 0; i < 2; i++) {
            int n = stan->bramki[sektor][i].zajetosc;
            int d = stan->bramki[sektor][i].druzyna;

            if (n < MAX_NA_STANOWISKU) {
                if (n == 0 || d == druzyna) { wybrane = i; break; }
                else powod = 1;
            } else {
                if (powod == 0) powod = 2;
            }
        }

        if (wybrane != -1) {
            bump_entered(stan, semid, wiek, is_kolega);

            stan->bramki[sektor][wybrane].zajetosc++;
            stan->bramki[sektor][wybrane].druzyna = druzyna;

            const char *tc = team_color(druzyna);
            const char *tn = team_name(druzyna);

            if (wiek < 15) {
                printf("[SEKTOR %d|ST %d] Wchodzi %s%s%s %s(+OPIEKUN)%s. Stan: %d/3\n",
                       sektor, wybrane,
                       tc, tn, CLR_RESET,
                       CLR_LBLUE, CLR_RESET,
                       stan->bramki[sektor][wybrane].zajetosc);
            } else {
                printf("[SEKTOR %d|ST %d] Wchodzi %s%s%s. Stan: %d/3\n",
                       sektor, wybrane,
                       tc, tn, CLR_RESET,
                       stan->bramki[sektor][wybrane].zajetosc);
            }

            fflush(stdout);

            sem_op(semid, sem_sektora, 1);
            usleep(300000);
            sem_op(semid, sem_sektora, -1);
            stan->bramki[sektor][wybrane].zajetosc--;
            sem_op(semid, sem_sektora, 1);
            if (!stan->ewakuacja_trwa) wszedl_do_sektora = 1;
            break;
        }

        sem_op(semid, sem_sektora, 1);

        cierpliwosc++;
        if (cierpliwosc >= LIMIT_CIERPLIWOSCI) {
            bump_agresja(stan, semid);
            printf(CLR_RED "[AGRESJA] KIBIC %d (DR %d) W SZALE POD SEKTOREM %d !!!" CLR_RESET "\n",
                   my_id, druzyna, sektor);
            fflush(stdout);
            break;
        }

        usleep(100000);
    }

    if (wszedl_do_sektora) {
        obecni_inc(stan, semid, sektor);
        while (!stan->ewakuacja_trwa) usleep(200000);
        obecni_dec(stan, semid, sektor);
    }

    if (shmdt(stan) == -1) warn_errno("shmdt");
    return 0;
}
