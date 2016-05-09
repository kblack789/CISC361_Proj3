# choose your compiler
CC=gcc -lkstat -DHAVE_KSTAT
#CC=gcc

mysh: sh.o get_path.o main.c which.c where.c list.c exitkillfree.c parse.c enviornment.c print.c signal.c alias.c wildcard.c
	$(CC) -g -pthread -lkstat -DHAVE_KSTAT main.c which.c where.c list.c exitkillfree.c parse.c enviornment.c print.c signal.c alias.c wildcard.c sh.o get_path.o -o mysh
#	$(CC) -g -pthread main.c which.c where.c list.c exitkillfree.c parse.c enviornment.c print.c signal.c alias.c wildcard.c sh.o get_path.o -o mysh

sh.o: sh.c sh.h
	$(CC) -g -c sh.c

get_path.o: get_path.c get_path.h
	$(CC) -g -c get_path.c

clean:
	rm -rf sh.o get_path.o mysh

