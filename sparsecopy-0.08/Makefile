CC=gcc
PREFIX=/usr
INSTALL=/usr/bin/install -c
BINDIR=${PREFIX}/bin

all: sparsecopy
sparsecopy: sparsecopy.c
	$(CC) -lm -o $@ $<

clean:
	/bin/rm -f *.o *~ sparsecopy

installbinaries: all
	${INSTALL} -m 755 sparsecopy ${BINDIR}

install: installbinaries
.PHONY: all clean install

