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

#endif