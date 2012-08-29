CFLAGS=-I.. -I../../.. -g

all: test

clean:
	rm -f t-elsa t-elsa.o

test: t-elsa
	./t-elsa

t-elsa: t-elsa.o
	$(CC) -g t-elsa.o -o t-elsa -lcrypto

t-elsa.o: t-elsa.c t-elsa.h $(wildcard ../elsa*.h) $(wildcard ../elsa*.c)

