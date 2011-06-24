# This makefile is too simple to require autoconf/imake/whatever.
# Edit as needed if SOCKS support or non-GCC/Linux support is needed.

#For SOCKS support, uncomment following lines
#SOCKS=-DSOCKS
#SLIB=-lsocks

#For broken (pre glibc2.1) versions of glibc
#and any other system that doesn't make semctl(2) vararg
#note that this assumes that union semun is defined by the headers
SEMCTLD=-DSEMCTL_NEEDS_ARG

#For systems that don't support ESIZE; used for hack around idiotic regex size
#limit
#REFLG=-DREG_ESIZE=0

# GCC:
CC=gcc -D_XOPEN_SOURCE=1 -D_XOPEN_SOURCE_EXTENDED=1 -D_GNU_SOURCE=1 -Wall -Wwrite-strings -Wstrict-prototypes $(SEMCTLD)
# XOPEN compliant systems:
#CC=c89 -D_XOPEN_SOURCE=1 -D_XOPEN_SOURCE_EXTENDED=1 # Sun: -D__EXTENSIONS__

#gcc
CFLAGS=-g3 -O3
#cc
#CFLAGS=-O

#for Solaris:
#LIBS=-lsocket -lnsl

IDIR=/u2/h0/dark
IDIR=/usr/local
BINDIR=$(IDIR)/bin
MANDIR=$(IDIR)/man/man1

INSTALL=install -c
INSTSTRIP=-s
INSTFLAGS=

EXE = lurkftp pipe
SCRIPT = atcron

all: $(EXE)

OBJS = lurkftp.o opt.o diff.o rept.o mir.o misc.o ftp.o ftpsupt.o break_lp.o

pipe: pipe.o
	$(CC) -o $@ pipe.o

lurkftp: $(OBJS) $(LIB)
	$(CC) -o $@ $(OBJS) $(LIB) $(LIBS) $(SLIB)

install: $(EXE)
	$(INSTALL) $(INSTFLAGS) $(INSTSTRIP) $(EXE) $(BINDIR)
	$(INSTALL) $(INSTFLAGS) $(SCRIPT) $(BINDIR)
	rm -f $(MANDIR)/lurkftp.1 $(MANDIR)/lurkftp.1.gz $(MANDIR)/lurkftp.1.Z
	$(INSTALL) lurkftp.1 $(MANDIR)/lurkftp.1

$(OBJS): lurkftp.h ftpsupt.h ftp.h

clean:
	rm -f *.o core

realclean: clean
	rm -f $(EXE) .chk* *.bak
