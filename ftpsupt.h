/*
 * Support routines' header for lurkftp
 * 
 * (C) 1997 Thomas J. Moore
 * questions/comments -> dark@mama.indstate.edu
 * 
 * This is free software.  No warranty for applicability or fitness is
 * expressed or implied.
 * 
 * This code may be modified and distributed freely so long as the original
 * author is credited and any changes are documented as such.
 * 
 */

/* all things needed by either ftpsupt.c or lurkftp.c */
/* Yes, I know, this violates standard coding practices, but I'm lazy */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <regex.h>
#include "ftp.h"
#include <sys/sysmacros.h>

#ifndef major
#waring old major/minor defs for Linux
# ifdef __linux__
#include <linux/kdev_t.h>
#define major MAJOR
#define minor MINOR
#define makedev MKDEV
#else
#define MINORBITS (sizeof(dev_t)/2)
#define MINORMASK (~0UL>>(sizeof(unsigned long)-MINORBITS))
#define major(x) ((x)>>MINORBITS)
#define minor(x) ((x)&MINORMASK)
#ifndef makedev
#define makedev(maj,min) (((maj)<<MINORBITS)|(min))
#endif
#warning you may want to include better defs for major/minor
#endif
#endif

extern char *crpg, *uncrpg; /* compression programs */

extern struct tm curtm;

#define BUFLEN (4096-sizeof(struct strmem *)-sizeof(short))

struct strmem {
    struct strmem *nxt;
    short ptr;
    char buf[BUFLEN];
};

struct fname {
    const char *dir, *name, *lnk, *root;
    mode_t mode;
    unsigned long size;
    short year;
    char month, day;
    signed char hr, min;
    char flags;
};

#define FNF_MOVE	1
#define FNF_APPEND	2
#define FNF_MODEONLY	4
#define FNF_DONE	8
#define FNF_PROCESS	16
#define FNF_DODIR	32

#define NSKIP 256

struct fnamemem {
    struct fnamemem *nxt;
    struct fname names[NSKIP];
};

struct dirmem {
    struct strmem *names;
    struct fnamemem *files;
    struct fname **array;
    unsigned long count;
};

enum {
    ERR_DIR = ERR_MAX,
    ERR_PWD,
    ERR_MAXRETR
};

struct myfile {
    void *data;
    int (*readln)(void *data, char *buf, int buflen);
    int (*read)(void *data, char *buf, int buflen);
    int (*write)(void *data, char *buf, int buflen);
};
    
