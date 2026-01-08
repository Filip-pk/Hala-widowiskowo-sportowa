#include "common.h"
#include <sys/wait.h>

#define MAX_PROC K

int main() {
    setbuf(stdout, NULL);

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

    /* shmat(): mapuje shm do pamięci procesu, żeby czytać/ustawiać stan symulacji*/
    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    /* Limit VIP*/
    int max_vip = (int)(K * 0.003);
    if (max_vip < 1) max_vip = 1;

    printf("--- START SYMULACJI ---\n");
    fflush(stdout);

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

    /* Generator kibiców*/
    int total_kibicow = (int)(K * 1.5);
    int active = 0, vip_cnt = 0;

    if (time(NULL) == (time_t)-1) warn_errno("time");
    srand((unsigned)time(NULL));
    sleep(1);

    for (int i = 0; i < total_kibicow; i++) {
        while (1) {
            /* waitpid(WNOHANG): zbiera zakończone dzieci żeby nie powstały zombie*/
            pid_t w = waitpid(-1, NULL, WNOHANG);
            if (w > 0) { active--; continue; }
            if (w == 0) break;
            if (errno == EINTR) continue;
            if (errno != ECHILD) warn_errno("waitpid(WNOHANG)");
            break;
        }

        if (active >= MAX_PROC) {
            while (1) {
                pid_t w = wait(NULL);
                if (w > 0) { active--; break; }
                if (errno == EINTR) continue;
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
        if (pk == -1) die_errno("fork(kibic)");
        if (pk == 0) {
            char id[20], v[8], r[8];
            sprintf(id, "%d", i);
            sprintf(v, "%d", is_vip);
            /* ~0.5% kibiców ma race */
            int has_raca = (rand() % 1000 < 5) ? 1 : 0;
            sprintf(r, "%d", has_raca);
            /* exec(): uruchamia ./kibic*/
            execl("./kibic", "kibic", id, v, r, NULL);
            die_errno("execl(kibic)");
        }

        active++;
        usleep(10000 + (rand() % 1000)); /* nie chcemy odpalić wszystkiego naraz*/
    }

    printf("[MAIN] Koniec generowania kibiców. Czekam na procesy...\n");
    fflush(stdout);

    /* Czekamy aż wszystkie dzieci zakończą pracę. */
    while (1) {
        /* wait(): zbiera wszystkie pozostałe procesy potomne*/
        pid_t w = wait(NULL);
        if (w > 0) continue;
        if (errno == EINTR) continue;
        if (errno != ECHILD) warn_errno("wait");
        break;
    }

    /* Dopisanie podsumowania do raportu*/
    FILE *rf = fopen("raport.txt", "a");
    if (rf) {
        fprintf(rf, "\nPODSUMOWANIE\n");
        fprintf(rf, "weszlo %d\n", stan->cnt_weszlo);
        fprintf(rf, "opiekun %d\n", stan->cnt_opiekun);
        fprintf(rf, "kolega %d\n", stan->cnt_kolega);
        fprintf(rf, "agresja %d\n", stan->cnt_agresja);
        if (fclose(rf) == EOF) warn_errno("fclose(raport.txt)");
    } else {
        warn_errno("fopen(raport.txt)");
    }

    /* shmdt(): odłącza pamięć współdzieloną od procesu main*/
    if (shmdt(stan) == -1) warn_errno("shmdt");

    /* Sprzątanie IPC*/
    if (system("./clean > /dev/null 2>&1") == -1) warn_errno("system(./clean)");
    return 0;
}
