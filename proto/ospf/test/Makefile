CFLAGS=-I.. -I../../.. -g

all: test

test: t-elsa
	./t-elsa

t-elsa: t-elsa.o

t-elsa.o: t-elsa.c t-elsa.h $(wildcard ../elsa*.h) $(wildcard ../elsa*.c)

