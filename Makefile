CC = gcc
CFLAGS = -Wall

all: setup clean_app kasjer kibic pracownik kierownik main monitor

setup: init.c common.h
	$(CC) $(CFLAGS) init.c -o setup

clean_app: clean.c common.h
	$(CC) $(CFLAGS) clean.c -o clean

kasjer: kasjer.c common.h
	$(CC) $(CFLAGS) kasjer.c -o kasjer

kibic: kibic.c common.h
	$(CC) $(CFLAGS) kibic.c -o kibic

pracownik: pracownik.c common.h
	$(CC) $(CFLAGS) pracownik.c -o pracownik

kierownik: kierownik.c common.h
	$(CC) $(CFLAGS) kierownik.c -o kierownik

main: main.c common.h
	$(CC) $(CFLAGS) main.c -o main

monitor: monitor.c common.h
	$(CC) $(CFLAGS) monitor.c -o monitor

reset:
	-./clean > /dev/null 2>&1 || true
	rm -f setup clean kasjer kibic pracownik kierownik main monitor