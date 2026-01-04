#include "common.h"

void print_timer(int sekundy) {
    int m = sekundy / 60;
    int s = sekundy % 60;
    printf("%02d:%02d", m, s);
}

int main() {
    int shmid = shmget(KEY_SHM, sizeof(SharedState), 0600);
    if (shmid == -1) {
        perror("shmget");
        fprintf(stderr, "Uruchom najpierw ./setup\n");
        exit(1);
    }

    SharedState *stan = (SharedState*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        perror("shmat");
        exit(1);
    }

    while (1) {
        if (stan->ewakuacja_trwa) break;
        printf("\033[H\033[J");
        printf("================================================================\n");
        if (stan->status_meczu == 0) {
            printf("OCZEKIWANIE NA MECZ (START ZA: ");
            printf("\033[1;33m");
            print_timer(stan->czas_pozostaly);
            printf("\033[0m)\n");
        } else if (stan->status_meczu == 1) {
            printf("\033[1;32mMECZ TRWA\033[0m (KONIEC ZA: ");
            printf("\033[1;37m");
            print_timer(stan->czas_pozostaly);
            printf("\033[0m)\n");
        } else {
            printf("\033[1;31mMECZ ZAKOŃCZONY - EWAKUACJA\033[0m\n");
        }
        printf("================================================================\n");

        printf("KOLEJKA PRZED HALĄ: Zwykli: %d | VIP: %d\n",
               stan->kolejka_zwykla, stan->kolejka_vip);

        printf("\n--- STATUS KAS ---\n");
        for (int i = 0; i < LICZBA_KAS; i++) {
            if (stan->aktywne_kasy[i]) printf("[ON ] ");
            else printf("[ . ] ");
        }
        printf("\n");

        printf("\n--- SPRZEDAŻ BILETÓW ---\n");
        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            printf("S%d: %3d | ", i, stan->sprzedane_bilety[i]);
            if ((i + 1) % 4 == 0) printf("\n");
        }
        int limit_vip = (int)(K * 0.003);
        if (limit_vip < 1) limit_vip = 1;
        printf("VIP: %3d / %d\n", stan->sprzedane_bilety[SEKTOR_VIP], limit_vip);

        printf("\n--- OBECNI NA HALI (WEDŁUG SEKTORÓW) ---\n");
        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            printf("S%d: %3d | ", i, stan->obecni_w_sektorze[i]);
            if ((i + 1) % 4 == 0) printf("\n");
        }
        printf("VIP: %3d\n", stan->obecni_w_sektorze[SEKTOR_VIP]);

        printf("\n--- KONTROLA BEZPIECZEŃSTWA ---\n");
        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            int n0 = stan->bramki[i][0].zajetosc;
            int d0 = stan->bramki[i][0].druzyna;
            int n1 = stan->bramki[i][1].zajetosc;
            int d1 = stan->bramki[i][1].druzyna;
            char s0[64], s1[64];
            if (n0 > 0) sprintf(s0, "[%d:%d]", n0, d0);
            else sprintf(s0, "[ . ]");
            if (n1 > 0) sprintf(s1, "[%d:%d]", n1, d1);
            else sprintf(s1, "[ . ]");
            printf("SEKTOR %d: %-10s | %-10s ", i, s0, s1);
            if (stan->blokada_sektora[i]) printf("[BLOKADA]");
            printf("\n");
        }

        fflush(stdout);
        usleep(500000);
    }

    shmdt(stan);
    return 0;
}
