CFLAGS?=	-Wall -ggdb -W -O
CC?=		gcc
LIBS?=	-lev
LDFLAGS?=
PREFIX?=	/usr/local
VERSION=0.1
TMPDIR=/tmp/webbench-$(VERSION)

all:   webbench

webbench: webbench.o Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o webbench webbench.o $(LIBS) 

clean:
	-rm -f *.o webbench *~ core *.core
	

webbench.o:	webbench.c socket.c Makefile

.PHONY: clean install all tar
