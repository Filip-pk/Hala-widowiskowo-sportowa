#include "common.h"

#include <fcntl.h>
#include <sys/file.h>
/*
 * ===================
 * KIBIC
 * ===================
 * Kibic jest osobnym procesem, który przechodzi etapy:
 *  1) (opcjonalnie) ustawienie się w kolejce do kas i wysłanie żądania (msg),
 *  2) odebranie biletu (msg) i zapis do raportu (plik + flock),
 *  3) wejście na stadion:
 *      - VIP: bez bramek, ale z kontrolą racy,
 *      - standard: przez 2 bramki w sektorze + limit osób + brak mieszania drużyn,
 *  4) siedzenie w sektorze (licznik obecnych w shm),
 *  5) wyjście przy ewakuacji (stan->ewakuacja_trwa).
 *
 * Kluczowe mechanizmy:
 *  - msg: komunikacja z kasjerami (żądanie i bilet),
 *  - shm: wspólny stan (blokady sektorów, bramki, ewakuacja, statystyki),
 *  - semafory: SEM_KASY dla kolejek, SEM_SEKTOR_* dla bramek, SEM_SHM dla liczników,
 *  - raport.txt: dopisywanie atomowe przez open()+flock()+dprintf().
 */


static void sem_op(int semid, int idx, int op) {
    struct sembuf sb = {(unsigned short)idx, (short)op, 0};
    while (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) _exit(0);
        die_errno("semop");
    }
}

/* Aktualizacja liczby obecnych w sektorze*/
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

/*
 * Dopisanie rekordu do raportu:
 *  - typ: vip / opiekun / zwykly
 *  - sektor: docelowy sektor (0..7 albo VIP)
 */
static void append_report(int kibic_id, int wiek, int sektor) {
    const char *typ = (sektor == SEKTOR_VIP) ? "vip" : ((wiek < 15) ? "opiekun" : "zwykly");

    /* open(): otwiera plik raportu do dopisywania*/
    int fd = open("raport.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) { warn_errno("open(raport.txt)"); return; }

    /* Blokada pliku, żeby wpisy z wielu procesów się nie mieszały*/
    if (flock(fd, LOCK_EX) == -1) {
        warn_errno("flock(LOCK_EX)");
        /* close(): zamyka deskryptor pliku*/
        if (close(fd) == -1) warn_errno("close(raport.txt)");
        return;
    }

    /* Dopisanie jednej linijki (id typ sektor)*/
    if (dprintf(fd, "%d %s %d\n", kibic_id, typ, sektor) < 0) {
        warn_errno("dprintf(raport.txt)");
    }

    if (flock(fd, LOCK_UN) == -1) warn_errno("flock(LOCK_UN)");
    /* close(): zamyka deskryptor pliku*/
    if (close(fd) == -1) warn_errno("close(raport.txt)");
}

/* Statystyki kto wszedł*/
static void bump_entered(SharedState *stan, int semid, int wiek, int is_kolega) {
    sem_op(semid, SEM_SHM, -1);
    stan->cnt_weszlo++;
    if (wiek < 15) stan->cnt_opiekun++;
    if (is_kolega) stan->cnt_kolega++;
    sem_op(semid, SEM_SHM, 1);
}

/* Statystyka agresji*/
static void bump_agresja(SharedState *stan, int semid) {
    sem_op(semid, SEM_SHM, -1);
    stan->cnt_agresja++;
    sem_op(semid, SEM_SHM, 1);
}

/*Wyproszenie kibica z racą*/
static void expel_for_flare(SharedState *stan, int semid, int sem_sektora, int sektor, int my_id) {
    printf(CLR_RED "[KONTROLA] WYKRYTO KIBICA %d Z RACĄ (SEKTOR %d) — WYPROSZONY!" CLR_RESET "\n",
           my_id, sektor);
    fflush(stdout);

    if (sektor >= 0 && sektor < LICZBA_SEKTOROW) {
        if (stan->agresor_sektora[sektor] == my_id) stan->agresor_sektora[sektor] = 0;
    }

    if (sem_sektora >= 0) sem_op(semid, sem_sektora, 1);

    if (shmdt(stan) == -1) warn_errno("shmdt");

    kill(getpid(), SIGKILL);

    _exit(137);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Użycie: %s <id> <vip> <ma_race> [ma_juz_bilet]\n", argv[0]);
        /* exit(): kończy proces*/
        exit(1);
    }

    int my_id = atoi(argv[1]);
    int is_vip = atoi(argv[2]);
    int ma_race = atoi(argv[3]);
    int ma_juz_bilet = (argc == 5) ? atoi(argv[4]) : 0;

    /* Losowanie wieku i drużyny*/
    srand(time(NULL) ^ (getpid() << 16));
    int wiek = 10 + rand() % 60;
    int druzyna = rand() % 2;

    /* Podłączenie do IPC. */
    /* shmget(): pobiera segment pamięci współdzielonej*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) { warn_errno("shmget"); exit(EXIT_FAILURE); }

    /* semget(): pobiera zestaw semaforów*/
    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) { warn_errno("semget"); exit(EXIT_FAILURE); }

    /* msgget(): pobiera kolejkę komunikatów*/
    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) { warn_errno("msgget"); exit(EXIT_FAILURE); }

    /* shmat(): mapuje shm do pamięci procesu*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    if (wiek < 15 && !is_vip) usleep(1000);
    if (stan->ewakuacja_trwa) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }
    if (!ma_juz_bilet && !is_vip && stan->standard_sold_out) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }
    if (!ma_juz_bilet && stan->sprzedaz_zakonczona) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

/*
 * =======================================
 * KOLEJKA DO KAS: shm + msg (SEM_KASY)
 * =======================================
 *  - Najpierw zwiększamy licznik kolejki w shm (kolejka_vip lub kolejka_zwykla).
 *    Robimy to pod SEM_KASY, bo ten licznik jest współdzielony z kasjerami.
 *  - Potem wysyłamy MsgKolejka na kolejkę msg:
 *      mtype = MSGTYPE_VIP_REQ albo MSGTYPE_STD_REQ,
 *      kibic_id = my_id.
 *
 * Kasjer odbiera te żądania i odsyła MsgBilet na typ:
 *  MSGTYPE_TICKET_BASE + my_id (unikalne „kanały” odpowiedzi per kibic).
 */

    /*Jeśli nie ma biletu: dołącza do kolejki i wysyła request do kasjera*/
    if (!ma_juz_bilet) {
        sem_op(semid, SEM_KASY, -1);
        if (is_vip) stan->kolejka_vip++;
        else stan->kolejka_zwykla++;
        sem_op(semid, SEM_KASY, 1);

        MsgKolejka req;
        req.mtype = is_vip ? MSGTYPE_VIP_REQ : MSGTYPE_STD_REQ;
        req.kibic_id = my_id;

        /* msgsnd(): wysyła żądanie do kolejki komunikatów*/
        while (msgsnd(msgid, &req, sizeof(int), 0) == -1) {
            if (errno == EINTR) continue;
            if (errno == EIDRM || errno == EINVAL) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }
            warn_errno("msgsnd(kolejka)");
            if (shmdt(stan) == -1) warn_errno("shmdt");
            exit(EXIT_FAILURE);
        }
    }

/*
 * ======================
 * OCZEKIWANIE NA BILET
 * ======================
 * Odbieramy bilet nieblokująco (IPC_NOWAIT), żeby móc przerwać oczekiwanie gdy:
 *  - ruszy ewakuacja,
 *  - sprzedaż się zakończy (sprzedaz_zakonczona).
 *
 * Dzięki temu kibic nie wisi na msgrcv().
 */

    /*Oczekiwanie na bilet*/
    MsgBilet bilet;
    while (1) {
        if (stan->ewakuacja_trwa) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }
        if (!ma_juz_bilet && stan->sprzedaz_zakonczona) { if (shmdt(stan) == -1) warn_errno("shmdt"); exit(0); }

        long my_ticket_type = MSGTYPE_TICKET_BASE + my_id;

        /* msgrcv(): odbiera odpowiedź-bilet z kolejki*/
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

/*
 * ==========================
 * RAPORT
 * ==========================
 * Każdy kibic dopisuje jedną linię: "id typ sektor".
 * Ponieważ działa wiele procesów naraz, używamy:
 *  - open(..., O_APPEND) żeby system dopisywał na koniec,
 *  - flock(LOCK_EX) żeby nie przeplatać wpisów.
 */
    append_report(my_id, wiek, sektor);
    const int is_kolega = (my_id >= DYN_ID_START);

/*
 * ==========
 * ŚCIEŻKA VIP
 * ==========
 * VIP omija bramki (nie ma konfliktu drużyn, nie ma limitu 3/stanowisko),
 * ale nadal jest kontrola bezpieczeństwa:
 *  - jeśli ma_race=1 -> natychmiastowe wyproszenie.
 */

    if (sektor == SEKTOR_VIP) {
        if (ma_race) {
            printf(CLR_RED "[KONTROLA VIP] WYKRYTO KIBICA %d Z RACĄ — WYPROSZONY!" CLR_RESET "\n", my_id);
            fflush(stdout);
            if (shmdt(stan) == -1) warn_errno("shmdt");
            kill(getpid(), SIGKILL);
            _exit(137);
        }

        bump_entered(stan, semid, wiek, is_kolega);
        printf(CLR_YELLOW "[VIP %d] WEJŚCIE VIP" CLR_RESET "\n", my_id);
        fflush(stdout);

        obecni_inc(stan, semid, SEKTOR_VIP);
        while (!stan->ewakuacja_trwa) usleep(200000);
        obecni_dec(stan, semid, SEKTOR_VIP);

        if (shmdt(stan) == -1) warn_errno("shmdt");
        exit(0);
    }

/*
 * ===================
 * ŚCIEŻKA STANDARD
 * ===================
 * Wejście przez bramki sektora:
 *  - sektor ma 2 bramki (Stanowisko[2]),
 *  - każda bramka ma limit MAX_NA_STANOWISKU (tu: 3),
 *  - nie mieszamy drużyn w tej samej bramce:
 *      jeśli bramka jest zajęta i druzyna != moja -> nie wchodzę.
 *
 * Synchronizacja:
 *  - SEM_SEKTOR_START + sektor chroni bramki[sektor][*] oraz agresor_sektora[sektor].
 *  - blokada_sektora[sektor] może być ustawiona komendami 1/2 od kierownika
 *    (pracownik sektora aktualizuje to w shm).
 */

    int sem_sektora = SEM_SEKTOR_START + sektor;
    int cierpliwosc = 0;
    int wszedl_do_sektora = 0;

    int tryb_agresora = 0;     /* po przekroczeniu cierpliwości */
    int agresja_ogloszona = 0;

    while (1) {
        if (stan->ewakuacja_trwa) break;

        /* Kierownik może zablokować sektor*/
        if (stan->blokada_sektora[sektor]) { usleep(200000); continue; }

        /* Semafor sektora: chroni stan bramek + agresor_sektora */
        sem_op(semid, sem_sektora, -1);

        /* Kontrola na bramkach: kibic z racą wylatuje*/
        if (ma_race) {
            expel_for_flare(stan, semid, sem_sektora, sektor, my_id);
        }
        if (stan->agresor_sektora[sektor] != 0 && stan->agresor_sektora[sektor] != my_id) {
            sem_op(semid, sem_sektora, 1);
            usleep(100000);
            continue;
        }

        /* Tryb agresora: rezerwujemy sektor, czekamy aż bramki puste i wchodzimy */
        if (tryb_agresora) {
            if (stan->agresor_sektora[sektor] == 0) stan->agresor_sektora[sektor] = my_id;

            if (stan->agresor_sektora[sektor] != my_id) {
                sem_op(semid, sem_sektora, 1);
                usleep(100000);
                continue;
            }

            int empty0 = (stan->bramki[sektor][0].zajetosc == 0);
            int empty1 = (stan->bramki[sektor][1].zajetosc == 0);

            if (!(empty0 && empty1)) {
                sem_op(semid, sem_sektora, 1);
                usleep(50000);
                continue;
            }

            /* bramki są puste => wchodzimy jako pierwsi */
            bump_entered(stan, semid, wiek, is_kolega);

            stan->bramki[sektor][0].zajetosc++;
            stan->bramki[sektor][0].druzyna = druzyna;

            stan->agresor_sektora[sektor] = 0; /* odblokuj wejście kolejnym */

            const char *tc = team_color(druzyna);
            const char *tn = team_name(druzyna);
            printf(CLR_RED "[AGRESOR %d] PRIORYTET! WCHODZI do bramki w sektorze %d: %s%s%s. Stan: %d/3" CLR_RESET "\n",
                   my_id, sektor, tc, tn, CLR_RESET,
                   stan->bramki[sektor][0].zajetosc);
            fflush(stdout);

            sem_op(semid, sem_sektora, 1);
            usleep(300000);

            sem_op(semid, sem_sektora, -1);
            if (stan->bramki[sektor][0].zajetosc > 0) stan->bramki[sektor][0].zajetosc--;
            sem_op(semid, sem_sektora, 1);

            if (!stan->ewakuacja_trwa) wszedl_do_sektora = 1;
            break;
        }

        int wybrane = -1;
        int powod = 0; // 1=konflikt drużyny, 2=pełno

        /* Szukamy bramki: albo pusta, albo zajęta przez naszą drużynę*/
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
            /* Udane wejście do bramki = liczymy jako wszedł w statystykach*/
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

            /* Zwolnienie semafora*/
            sem_op(semid, sem_sektora, 1);
            usleep(300000);

            /* Aktualizacja bramki po przejściu*/
            sem_op(semid, sem_sektora, -1);
            if (stan->bramki[sektor][wybrane].zajetosc > 0) stan->bramki[sektor][wybrane].zajetosc--;
            sem_op(semid, sem_sektora, 1);

            if (!stan->ewakuacja_trwa) wszedl_do_sektora = 1;
            break;
        }

        /* Nie udało się wejść — próbuj później*/
        sem_op(semid, sem_sektora, 1);

/*
 * =============================
 * AGRESJA
 * =============================
 * Gdy kibic długo nie może wejść (LIMIT_CIERPLIWOSCI prób),
 * włącza tryb agresora:
 *  - zwiększamy licznik cnt_agresja (statystyka),
 *  - rezerwujemy sektor przez agresor_sektora[sektor] = my_id,
 *  - czekamy aż obie bramki będą puste,
 *  - wchodzimy jako pierwsi i zwalniamy rezerwację.
 *
 * To modeluje sytuację „ktoś się przepycha” i chwilowo blokuje innych.
 */

        cierpliwosc++;
        if (cierpliwosc >= LIMIT_CIERPLIWOSCI) {
            if (!agresja_ogloszona) {
                bump_agresja(stan, semid);
                printf(CLR_RED "[AGRESJA] KIBIC %d (DR %d) POD SEKTOREM %d — BIERZE PRIORYTET!" CLR_RESET "\n",
                       my_id, druzyna, sektor);
                fflush(stdout);
                agresja_ogloszona = 1;
            }
            tryb_agresora = 1;
        }

        usleep(100000);
    }

    if (tryb_agresora) {
        sem_op(semid, sem_sektora, -1);
        if (stan->agresor_sektora[sektor] == my_id) stan->agresor_sektora[sektor] = 0;
        sem_op(semid, sem_sektora, 1);
    }

    if (wszedl_do_sektora) {
        obecni_inc(stan, semid, sektor);
        while (!stan->ewakuacja_trwa) usleep(200000);
        obecni_dec(stan, semid, sektor);
    }

    /* shmdt(): odłącza shm od procesu*/
    if (shmdt(stan) == -1) warn_errno("shmdt");
    return 0;
}
