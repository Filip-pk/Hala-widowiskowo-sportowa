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
    int active = 0;

    srand(time(NULL));
    sleep(1);

    for (int i = 0; i < total_kibicow; i++) {
        while (waitpid(-1, NULL, WNOHANG) > 0) active--;
        if (active >= MAX_PROC) { wait(NULL); active--; }

        if (stan->ewakuacja_trwa) break;

        if (!fork()) {
            char id[20];
            sprintf(id, "%d", i);
            execl("./kibic", "kibic", id, "0", NULL);
            perror("execl");
            exit(1);
        }

        active++;
        usleep(10000);
    }

    printf("[MAIN] Koniec generowania kibiców. Czekam na procesy...\n");
    fflush(stdout);

    while (wait(NULL) > 0);
    shmdt(stan);
    return 0;
}
