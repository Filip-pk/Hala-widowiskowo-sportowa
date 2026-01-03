#include "common.h"
#include <sys/wait.h>

#define MAX_PROC K

int main() {
    setbuf(stdout, NULL);

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) {
        perror("shmget");
        fprintf(stderr, "Uruchom najpierw ./setup\n");
        exit(1);
    }

    int semid = semget(KEY_SEM, 0, 0600);
    if (semid == -1) { perror("semget"); exit(1); }

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) { perror("msgget"); exit(1); }

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) { perror("shmat"); exit(1); }

    int max_vip = (int)(K * 0.003);
    if (max_vip < 1) max_vip = 1;

    printf("--- START SYMULACJI ---\n");
    fflush(stdout);

    if (!fork()) {
        execl("./kierownik", "kierownik", NULL);
        perror("execl");
        exit(1);
    }

    for (int i = 0; i < LICZBA_SEKTOROW; i++)
        if (!fork()) {
            char b[10];
            sprintf(b, "%d", i);
            execl("./pracownik", "pracownik", b, NULL);
            perror("execl");
            exit(1);
        }

    for (int i = 0; i < LICZBA_KAS; i++)
        if (!fork()) {
            char b[10];
            sprintf(b, "%d", i);
            execl("./kasjer", "kasjer", b, NULL);
            perror("execl");
            exit(1);
        }

    int total_kibicow = (int)(K * 1.5);
    int active = 0, vip_cnt = 0;

    srand(time(NULL));
    sleep(1);

    for (int i = 0; i < total_kibicow; i++) {
        while (waitpid(-1, NULL, WNOHANG) > 0) active--;
        if (active >= MAX_PROC) {
            wait(NULL);
            active--;
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

        if (!fork()) {
            char id[20], v[8];
            sprintf(id, "%d", i);
            sprintf(v, "%d", is_vip);
            execl("./kibic", "kibic", id, v, NULL);
            perror("execl");
            exit(1);
        }

        active++;
        usleep(10000 + (rand() % 1000));
    }

    printf("[MAIN] Koniec generowania kibiców. Czekam na procesy...\n");
    fflush(stdout);

    while (wait(NULL) > 0);

    FILE *rf = fopen("raport.txt", "a");
    if (rf) {
        fprintf(rf, "\nPODSUMOWANIE\n");
        fprintf(rf, "weszlo %d\n", stan->cnt_weszlo);
        fprintf(rf, "opiekun %d\n", stan->cnt_opiekun);
        fprintf(rf, "kolega %d\n", stan->cnt_kolega);
        fprintf(rf, "agresja %d\n", stan->cnt_agresja);
        fclose(rf);
    }

    shmdt(stan);
    system("./clean > /dev/null 2>&1");
    return 0;
}
