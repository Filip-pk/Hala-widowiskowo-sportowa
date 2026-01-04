#include "common.h"
#include <sys/wait.h>

#define MAX_PROC K

int main() {
    setbuf(stdout, NULL);

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) {
        warn_errno("shmget");
        fprintf(stderr, "Uruchom najpierw ./setup\n");
        exit(EXIT_FAILURE);
    }

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) die_errno("semget");

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) die_errno("msgget");

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    int max_vip = (int)(K * 0.003);
    if (max_vip < 1) max_vip = 1;

    printf("--- START SYMULACJI ---\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid == -1) die_errno("fork(kierownik)");
    if (pid == 0) {
        execl("./kierownik", "kierownik", NULL);
        die_errno("execl(kierownik)");
    }

    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        pid_t p = fork();
        if (p == -1) die_errno("fork(pracownik)");
        if (p == 0) {
            char b[10];
            sprintf(b, "%d", i);
            execl("./pracownik", "pracownik", b, NULL);
            die_errno("execl(pracownik)");
        }
    }

    for (int i = 0; i < LICZBA_KAS; i++) {
        pid_t p = fork();
        if (p == -1) die_errno("fork(kasjer)");
        if (p == 0) {
            char b[10];
            sprintf(b, "%d", i);
            execl("./kasjer", "kasjer", b, NULL);
            die_errno("execl(kasjer)");
        }
    }

    int total_kibicow = (int)(K * 1.5);
    int active = 0, vip_cnt = 0;

    if (time(NULL) == (time_t)-1) warn_errno("time");
    srand((unsigned)time(NULL));
    sleep(1);

    for (int i = 0; i < total_kibicow; i++) {
        while (1) {
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

        if (stan->ewakuacja_trwa) break;
        if (stan->sprzedaz_zakonczona) break;

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

        pid_t pk = fork();
        if (pk == -1) die_errno("fork(kibic)");
        if (pk == 0) {
            char id[20], v[8];
            sprintf(id, "%d", i);
            sprintf(v, "%d", is_vip);
            execl("./kibic", "kibic", id, v, NULL);
            die_errno("execl(kibic)");
        }

        active++;
        usleep(10000 + (rand() % 1000));
    }

    printf("[MAIN] Koniec generowania kibiców. Czekam na procesy...\n");
    fflush(stdout);

    while (1) {
        pid_t w = wait(NULL);
        if (w > 0) continue;
        if (errno == EINTR) continue;
        if (errno != ECHILD) warn_errno("wait");
        break;
    }

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

    if (shmdt(stan) == -1) warn_errno("shmdt");
    if (system("./clean > /dev/null 2>&1") == -1) warn_errno("system(./clean)");
    return 0;
}
