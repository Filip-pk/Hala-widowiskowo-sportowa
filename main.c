#include "common.h"
#include <sys/wait.h>

#define MAX_PROC K
/*
 * Zadaniem pliku jest:
 *  1) podpiąć IPC (shm/sem/msg) utworzone wcześniej przez ./setup,
 *  2) uruchomić procesy: kierownik, pracownicy sektorów, kasjerzy,
 *  3) generować procesy kibiców w sposób kontrolowany (limit MAX_PROC),
 *  4) na końcu zebrać wszystkie dzieci.
 *
 * Dlaczego limit MAX_PROC + waitpid(WNOHANG)?
 *  - Kibiców jest dużo a generator robi K*1.5 prób,
 *  - gdyby odpalić wszystkich naraz, system dostałby burst tysięcy procesów,
 *    a semafory/kolejki nie miałyby sensu (wszystko w tym samym momencie).
 *  - dlatego main dba o to, by maksymalnie K procesów dzieci było aktywnych
 *    jednocześnie, a zakończone procesy są zbierane (żeby nie robić zombie).
 */

static volatile sig_atomic_t g_stop = 0;

static void on_stop_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int sem_op_blocking(int semid, unsigned short num, short op) {
    struct sembuf sb;
    sb.sem_num = num;
    sb.sem_op  = op;
    sb.sem_flg = 0;

    while (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static void request_shutdown(SharedState *stan, int semid) {
    if (sem_op_blocking(semid, SEM_SHM, -1) == -1) return;
    stan->sprzedaz_zakonczona = 1;
    stan->ewakuacja_trwa = 1;
    (void)sem_op_blocking(semid, SEM_SHM, +1);
}

int main() {
    setbuf(stdout, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) warn_errno("sigaction(SIGINT)");
    if (sigaction(SIGTERM, &sa, NULL) == -1) warn_errno("sigaction(SIGTERM)");

    /* shmget(): pobiera istniejący segment pamięci współdzielonej*/
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) {
        warn_errno("shmget");
        fprintf(stderr, "Uruchom najpierw ./setup\n");
        /* exit(): kończy proces z kodem błędu*/
        exit(EXIT_FAILURE);
    }

    /* semget(): pobiera istniejący zestaw semaforów*/
    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) die_errno("semget");

    /* msgget(): pobiera istniejącą kolejkę komunikatów*/
    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) die_errno("msgget");
    (void)msgid;

    /* shmat(): mapuje shm do pamięci procesu, żeby czytać/ustawiać stan symulacji*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    /* Limit VIP*/
    int max_vip = (int)(K * 0.003);
    if (max_vip < 1) max_vip = 1;

    printf("--- START SYMULACJI ---\n");
    fflush(stdout);

/*
 * ======================
 * URUCHAMIANIE PROCESÓW
 * ======================
 * Każdy element symulacji jest osobnym procesem:
 *  - kierownik: steruje sygnałami 1/2/3 i (jako master) zegarem meczu,
 *  - pracownik(sektor): wykonuje blokadę/odblokowanie i ewakuację sektora,
 *  - kasjer(kasa): obsługuje kolejki, sprzedaje bilety,
 *  - kibic: klient kas + próba wejścia przez bramki + zachowania (agresja).
 *
 * Cel: realistyczny równoległy dostęp do wspólnego stanu -> shm + semafory.
 */

    /* Start procesu kierownika. */
    /* fork(): tworzy proces*/
    pid_t pid = fork();
    if (pid == -1) die_errno("fork(kierownik)");
    if (pid == 0) {
        /* exec(): uruchamia program ./kierownik*/
        execl("./kierownik", "kierownik", NULL);
        die_errno("execl(kierownik)");
    }

    /* Start 8 pracowników*/
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        /* fork(): tworzy proces*/
        pid_t p = fork();
        if (p == -1) die_errno("fork(pracownik)");
        if (p == 0) {
            char b[10];
            sprintf(b, "%d", i);
            /* exec(): uruchamia ./pracownik*/
            execl("./pracownik", "pracownik", b, NULL);
            die_errno("execl(pracownik)");
        }
    }

    /* Start kasjerów*/
    for (int i = 0; i < LICZBA_KAS; i++) {
        /* fork(): tworzy proces*/
        pid_t p = fork();
        if (p == -1) die_errno("fork(kasjer)");
        if (p == 0) {
            char b[10];
            sprintf(b, "%d", i);
            /* exec(): uruchamia ./kasjer*/
            execl("./kasjer", "kasjer", b, NULL);
            die_errno("execl(kasjer)");
        }
    }

/*
 * =================
 * GENERATOR KIBICÓW
 * =================
 * Tworzymy więcej „prób” niż miejsc (K * 1.5), bo
 * chcemy, żeby system kas i bramek miał realne obciążenie.
 *
 * VIP:
 *  - limit max_vip ~ 0.3% K,
 *  - VIP jest losowany
 *
 * Raca:
 *  - ~0.5% kibiców ma racę (has_raca=1) -> kontrola przy wejściu wyrzuca.
 */

    /* Generator kibiców*/
    int total_kibicow = (int)(K * 1.05);
    int active = 0, vip_cnt = 0;

    if (time(NULL) == (time_t)-1) warn_errno("time");
    srand((unsigned)time(NULL));
    sleep(1);

    for (int i = 0; i < total_kibicow; i++) {
        /* Jeśli Ctrl+C, kończymy generowanie i przechodzimy do sprzątania*/
        if (g_stop) {
            request_shutdown(stan, semid);
            break;
        }

        while (1) {
            /* waitpid(WNOHANG): zbiera zakończone dzieci żeby nie powstały zombie*/
            pid_t w = waitpid(-1, NULL, WNOHANG);
            if (w > 0) { active--; continue; }
            if (w == 0) break;

            if (errno == EINTR) {
                if (g_stop) break;
                continue;
            }
            if (errno != ECHILD) warn_errno("waitpid(WNOHANG)");
            break;
        }

        if (active >= MAX_PROC) {
            while (1) {
                pid_t w = wait(NULL);
                if (w > 0) { active--; break; }
                if (errno == EINTR) {
                    if (g_stop) break;
                    continue;
                }
                if (errno != ECHILD) warn_errno("wait");
                break;
            }
        }

        /* Przerywamy generowanie, gdy trwa ewakuacja lub sprzedaż jest zakończona*/
        if (stan->ewakuacja_trwa) break;
        if (stan->sprzedaz_zakonczona) break;

        /*VIP losowo (~0.3%) albo wymuszeni na końcu, żeby dobić do max_vip*/
        int is_vip = 0;
        if (stan->standard_sold_out) {
            if (vip_cnt < max_vip) {
                is_vip = 1;
                vip_cnt++;
            } else {
                break;
            }
        } else {
            if (vip_cnt < max_vip) {
                if ((rand() % 1000 < 3) || (total_kibicow - i <= max_vip - vip_cnt)) {
                    is_vip = 1;
                    vip_cnt++;
                }
            }
        }

        /* Tworzymy proces kibica*/
        /* fork(): tworzy proces*/
        pid_t pk = fork();
        if (pk == -1) {
            warn_errno("fork(kibic)");
            request_shutdown(stan, semid);
            g_stop = 1;
            break;
        }
        if (pk == 0) {
            char id[20], v[8], r[8];
            sprintf(id, "%d", i);
            sprintf(v, "%d", is_vip);

            /* ~0.5% kibiców ma race*/
            int has_raca = (rand() % 1000 < 5) ? 1 : 0;
            sprintf(r, "%d", has_raca);

            /* exec(): uruchamia ./kibic*/
            execl("./kibic", "kibic", id, v, r, NULL);
            die_errno("execl(kibic)");
        }

        active++;
        usleep(10000 + (rand() % 1000)); /* nie chcemy odpalić wszystkiego naraz*/
    }

    if (g_stop) {
        printf("\n[MAIN] Przerwano sygnałem. Kończę procesy i sprzątam IPC...\n");
        fflush(stdout);
        if (killpg(getpgrp(), SIGTERM) == -1 && errno != ESRCH) {
            warn_errno("killpg(SIGTERM)");
        }
    }

    printf("[MAIN] Koniec generowania kibiców. Czekam na procesy...\n");
    fflush(stdout);

    /*
     * Czekamy aż wszystkie dzieci zakończą pracę.
     * Przy Ctrl+C nie chcemy wisieć w wait() w nieskończoność
     */
    int spin = 0;
    while (1) {
        pid_t w = waitpid(-1, NULL, WNOHANG);
        if (w > 0) continue;

        if (w == 0) {
            if (!g_stop) {
                w = wait(NULL);
                if (w > 0) continue;
                if (errno == EINTR) continue;
                if (errno != ECHILD) warn_errno("wait");
                break;
            }

            usleep(20000);
            if (++spin == 100) {
                if (killpg(getpgrp(), SIGKILL) == -1 && errno != ESRCH) {
                    warn_errno("killpg(SIGKILL)");
                }
            }
            continue;
        }

        if (errno == EINTR) continue;
        if (errno == ECHILD) break;

        warn_errno("waitpid");
        break;
    }

    /* shmdt(): odłącza pamięć współdzieloną od procesu main*/
    if (shmdt(stan) == -1) warn_errno("shmdt");

    /* Sprzątanie IPC*/
    if (system("./clean > /dev/null 2>&1") == -1) warn_errno("system(./clean)");
    return 0;
}