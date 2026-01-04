#include "common.h"
#include <sys/wait.h>
#include <time.h>

static void ewakuacja(int msgid, SharedState *stan) {
    stan->ewakuacja_trwa = 1;
    stan->status_meczu = 2;

    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        MsgSterujacy msg = {10 + i, 3, i};
        if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) return;
            warn_errno("msgsnd(ewakuacja)");
        }
    }

    printf("[KIEROWNIK] EWAKUACJA\n");
    fflush(stdout);

    int raporty = 0;
    while (raporty < LICZBA_SEKTOROW) {
        MsgSterujacy rap;
        ssize_t res = msgrcv(msgid, &rap, sizeof(int) * 2, 99, 0);
        if (res >= 0) {
            printf("[RAPORT] Sektor %d pusty\n", rap.sektor_id);
            fflush(stdout);
            raporty++;
        } else {
            if (errno == EIDRM || errno == EINVAL) break;
            if (errno == EINTR) continue;
            warn_errno("msgrcv(raport)");
            break;
        }
    }

    while (stan->obecni_w_sektorze[SEKTOR_VIP] > 0) {
        usleep(100000);
    }

    printf("[KIEROWNIK] Koniec symulacji\n");
    fflush(stdout);
}

int main() {
    setbuf(stdout, NULL);

    int msgid = msgget(KEY_MSG, 0600);
    if (msgid == -1) die_errno("msgget");

    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) die_errno("shmget");

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) die_errno("shmat");

    stan->status_meczu = 0;
    stan->czas_pozostaly = CZAS_PRZED_MECZEM;

    pid_t zegar_pid = fork();
    if (zegar_pid == -1) die_errno("fork(zegar)");
    if (zegar_pid == 0) {
        time_t start = time(NULL);
        if (start == (time_t)-1) die_errno("time");
        while (1) {
            time_t now = time(NULL);
            if (now == (time_t)-1) die_errno("time");
            int left = CZAS_PRZED_MECZEM - (int)(now - start);
            if (left <= 0) break;
            stan->czas_pozostaly = left;
            sleep(1);
        }

        stan->status_meczu = 1;
        stan->czas_pozostaly = CZAS_MECZU;

        start = time(NULL);
        if (start == (time_t)-1) die_errno("time");
        while (1) {
            time_t now = time(NULL);
            if (now == (time_t)-1) die_errno("time");
            int left = CZAS_MECZU - (int)(now - start);
            if (left <= 0) break;
            stan->czas_pozostaly = left;
            sleep(1);
        }

        stan->status_meczu = 2;
        stan->czas_pozostaly = 0;
        exit(0);
    }

    printf("Komendy: 1-stop, 2-start, 3-ewakuacja\n");
    fflush(stdout);

    fd_set readfds;
    struct timeval tv;

    while (1) {
        while (1) {
            pid_t w = waitpid(zegar_pid, NULL, WNOHANG);
            if (w > 0) { ewakuacja(msgid, stan); goto out; }
            if (w == 0) break;
            if (errno == EINTR) continue;
            warn_errno("waitpid(WNOHANG)");
            break;
        }

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            if (errno == EINTR) continue;
            warn_errno("select");
            continue;
        }
        if (ret > 0) {
            int cmd;
            if (scanf("%d", &cmd) == 1) {
                if (cmd == 3) {
                    if (kill(zegar_pid, SIGTERM) == -1 && errno != ESRCH) warn_errno("kill(zegar)");
                    while (waitpid(zegar_pid, NULL, 0) == -1 && errno == EINTR) {}
                    ewakuacja(msgid, stan);
                    break;
                }
                if (cmd == 1 || cmd == 2) {
                    int s;
                    printf("Sektor (0-7): ");
                    fflush(stdout);
                    if (scanf("%d", &s) == 1 && s >= 0 && s < LICZBA_SEKTOROW) {
                        MsgSterujacy msg = {10 + s, cmd, s};
                        if (msgsnd(msgid, &msg, sizeof(int) * 2, 0) == -1) {
                            if (!(errno == EIDRM || errno == EINVAL)) warn_errno("msgsnd(sterowanie)");
                        }
                    }
                }
            }
        }
    }

out:

    if (shmdt(stan) == -1) warn_errno("shmdt");
    if (kill(zegar_pid, SIGTERM) == -1 && errno != ESRCH) warn_errno("kill(zegar)");
    while (waitpid(zegar_pid, NULL, 0) == -1 && errno == EINTR) {}
    return 0;
}
