#ifndef COMMON_H
#define COMMON_H

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

#define K 2000
#define LICZBA_SEKTOROW 8
#define SEKTOR_VIP 8
#define LICZBA_KAS 10
#define MAX_NA_STANOWISKU 3
#define LIMIT_CIERPLIWOSCI 5
#define CZAS_PRZED_MECZEM 10
#define CZAS_MECZU 200

#define KEY_SHM 1234
#define KEY_SEM 5678
#define KEY_MSG 9012

typedef struct {
    int zajetosc;
    int druzyna;
} Stanowisko;

typedef struct {
    int kolejka_zwykla;
    int kolejka_vip;
    int aktywne_kasy[LICZBA_KAS];
    int sprzedane_bilety[LICZBA_SEKTOROW + 1];
    Stanowisko bramki[LICZBA_SEKTOROW][2];
    int blokada_sektora[LICZBA_SEKTOROW];
    int ewakuacja_trwa;
    int status_meczu;
    int czas_pozostaly;

    int next_kibic_id;

    int standard_sold_out;
    int sprzedaz_zakonczona;

    int cnt_weszlo;
    int cnt_opiekun;
    int cnt_kolega;
    int cnt_agresja;
} SharedState;

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

#define MSGTYPE_VIP_REQ 1
#define MSGTYPE_STD_REQ 2
#define MSGTYPE_TICKET_BASE 10000

#define DYN_ID_START 50000

#define SEM_SHM 0
#define SEM_KASY 1
#define SEM_SEKTOR_START 2

#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[1;31m"
#define CLR_GREEN   "\033[1;32m"
#define CLR_YELLOW  "\033[1;33m"
#define CLR_PURPLE  "\033[1;35m"
#define CLR_DBLUE   "\033[0;34m"
#define CLR_LBLUE   "\033[1;36m"

#endif
