#ifndef COMMON_H
#define COMMON_H

 /* Wspólne definicje dla całej symulacji”.
 * Mamy tutaj:
 *  - stałe konfiguracyjne (pojemności, czasy, limity),
 *  - klucze IPC (System V: shm/sem/msg),
 *  - struktury pamięci współdzielonej (SharedState),
 *  - formaty komunikatów (kolejka wiadomości),
 *  - indeksy semaforów oraz sekwencje ANSI do kolorowania logów.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

/*
 * Helpery do diagnostyki błędów systemowych.
 * Użycie:
 *   if (shmget(...) == -1) die_errno("shmget");
 * warn_errno() wypisuje błąd (perror) + numer errno i jego opis,
 * die_errno() robi to samo, a potem kończy proces kodem EXIT_FAILURE.
 */
static inline void warn_errno(const char *ctx) {
    int e = errno;
    perror(ctx);
    fprintf(stderr, "errno=%d (%s)\n", e, strerror(e));
}

static inline void die_errno(const char *ctx) {
    warn_errno(ctx);
    exit(EXIT_FAILURE);
}

/*
 * KOLEJKA WIADOMOŚCI (msg) - najważniejsze typy:
 *  - MSGTYPE_VIP_REQ (1):   kibic VIP -> kasjer (prośba o bilet VIP),
 *  - MSGTYPE_STD_REQ (2):   kibic STD -> kasjer (prośba o bilet standard),
 *  - MSGTYPE_TICKET_BASE+id: kasjer -> konkretny kibic (odpowiedź: sektor lub -1),
 *  - mtype = 10 + sektor:   kierownik -> pracownik(sektor) (sygnał 1/2/3),
 *  - mtype = 99:            pracownik -> kierownik (raport: sektor pusty),
 *  - mtype = 5000:          kontroler-kierownik -> master-kierownik (przekaz komend).
 */

/* =========================
 * Parametry symulacji
 * ========================= */

/* K – liczba kibicow. */
#define K 2000

/* Sektory standardowe*/
#define LICZBA_SEKTOROW 8

/* Sektor VIP*/
#define SEKTOR_VIP 8

/* Liczba kas*/
#define LICZBA_KAS 10

/* Maksymalna liczba osób jednocześnie w jednej bramce*/
#define MAX_NA_STANOWISKU 3

/* Licznik prób wejścia kibica*/
#define LIMIT_CIERPLIWOSCI 5

/* Czas do rozpoczęcia meczu w sekundach*/
#define CZAS_PRZED_MECZEM 10

/* Czas trwania meczu w sekundach*/
#define CZAS_MECZU 200

/* =========================
 * Klucze IPC
 * =========================
 */
#define KEY_SHM 1234
#define KEY_SEM 5678
#define KEY_MSG 9012

/* =========================
 * Struktury danych
 * =========================
 * Stanowisko = jedna bramka wejściowa.
 *
 * zajetosc – ilu kibiców aktualnie jest w bramce
 * druzyna  – która drużyna jest aktualnie w bramce
 */
typedef struct {
    int zajetosc;
    int druzyna;
} Stanowisko;

typedef struct {
    /* Aktualne długości kolejek*/
    int kolejka_zwykla;
    int kolejka_vip;

    /* Czy dana kasa jest aktywna*/
    int aktywne_kasy[LICZBA_KAS];

    /* Ile biletów sprzedano na każdy sektor*/
    int sprzedane_bilety[LICZBA_SEKTOROW + 1];

    /* 2 bramki dla kazdego sektora*/
    Stanowisko bramki[LICZBA_SEKTOROW][2];

    /* Ile osób aktualnie przebywa w sektorze*/
    int obecni_w_sektorze[LICZBA_SEKTOROW + 1];

    /* Flagi blokad sektorów standardowych*/
    int blokada_sektora[LICZBA_SEKTOROW];

    /*Priorytet „agresora” pod sektorem.*/
    int agresor_sektora[LICZBA_SEKTOROW];

    /* Flaga globalna: trwa ewakuacja (1) / nie trwa (0). */
    int ewakuacja_trwa;

    /* Status meczu*/
    int status_meczu;

    /* Licznik czasu w sekundach. */
    int czas_pozostaly;

    /* Generator unikalnych ID dla kibicow*/
    int next_kibic_id;

    /* Flagi wyprzedania biletow i zakończenia sprzedaży. */
    int standard_sold_out;
    int sprzedaz_zakonczona;

    /* Statystyki do raportu końcowego. */
    int cnt_weszlo;
    int cnt_opiekun;
    int cnt_kolega;
    int cnt_agresja;
} SharedState;

/* =========================
 * Komunikaty kolejki
 * =========================
 */
typedef struct {
    long mtype;
    int sektor_id;
} MsgBilet;

typedef struct {
    long mtype;
    int typ_sygnalu;
    int sektor_id;
} MsgSterujacy;

typedef struct {
    long mtype;
    int kibic_id;
} MsgKolejka;

/* Typy wiadomości (vip lub zwykly kibic)*/
#define MSGTYPE_VIP_REQ 1
#define MSGTYPE_STD_REQ 2
#define MSGTYPE_TICKET_BASE 10000

/* ID dla kolegow ktorzy nie pojawili sie w kasie*/
#define DYN_ID_START 50000

/* =========================
 * Semafory
 * =========================
 *  - SEM_SHM: ochrona dostępu do SharedState
 *  - SEM_KASY: koordynacja kas
 *  - SEM_SEKTOR_START..: semafory per-sektor
 */
#define SEM_SHM 0
#define SEM_KASY 1
#define SEM_SEKTOR_START 2
#define SEM_KIEROWNIK (SEM_SEKTOR_START + LICZBA_SEKTOROW)

/* =========================
 * Kolory ANSI do logów
 * ========================= */

#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[1;31m"
#define CLR_GREEN   "\033[1;32m"
#define CLR_YELLOW  "\033[1;33m"
#define CLR_PURPLE  "\033[1;35m"
#define CLR_DBLUE   "\033[0;34m"
#define CLR_LBLUE   "\033[1;36m"

#endif