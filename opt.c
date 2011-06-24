/*
 * Option parsing for lurkftp
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

#include "lurkftp.h"
#include <stddef.h> /* for offsetof() */
#include <pwd.h>

struct op *ops;
static struct op defop = {0}, **nxtop = &ops;

static char *anonpasswd;
static char *lsbase; /* default lsfile name */

static void help(int ret)
{
    fprintf(stderr,PROGRAMNAME ": monitor and/or mirror FTP sites\n\n"
	    "usage: %s [[options] [<site> [<dir>]+]]+\n\n",progname);
    fputs("(C) 1997 Thomas J. Moore\n",stderr);
    exit(ret);
}

static void parseopts(const char *(*)(void *,int), void *);

struct ofile {
    FILE *f;
    char buf[4096], pbuf[4096];
    char *bptr;
};

/* returns pointer to end of string or NULL if error */
static char *homesubst(char *buf)
{
    struct passwd *pw;
    char *s;

    if(!*buf) {
	s = getenv("HOME");
	if(s) {
	    strcpy(buf,s);
	    return buf+strlen(buf);
	} else {
	    pw = getpwuid(getuid());
	    if(!pw) {
		perror("My own homedir");
		help(1);
	    }
	}
    } else {
	pw = getpwnam(buf);
	if(!pw) {
	    strcat(buf,"'s homedir");
	    perror(buf);
	    help(1);
	}
    }
    strcpy(buf,pw->pw_dir);
    return buf+strlen(buf);
}

static const char *readfile(void *data, int getopt)
{
    struct ofile *of = data;
    char *s, *p, *d, quote, home;

    if(!of->f)
      return NULL;
    if (!*of->bptr || *of->bptr == '#') {
	do {
	    if(!fgets(of->buf,4096,of->f)) {
		fclose(of->f);
		of->f = NULL;
		return NULL;
	    }
	    of->bptr = of->buf;
	    while(isspace(*of->bptr))
	      of->bptr++;
	} while(!*of->bptr || *of->bptr == '#');
	for(s=of->bptr+strlen(of->bptr)-1;isspace(*s);s--);
	*++s = 0;
	if(defop.srcls.site.host) {
	    if(debug & DB_OPT)
	      fputs("optfile: '\\n' -> '-+'\n",stderr);
	    return "-+";
	}
    }
    d = of->pbuf;
    if(!getopt)
      d += strlen(d)+1;
    p = of->bptr;
    quote = 0;
    if(*p == '~') {
	p++;
	home = 1;
    } else
      home = 0;
    while(*p) {
	if(!quote && *p == '\'') {
	    while(*++p && *p != '\'')
	      *d++ = *p;
	    p++;
	    continue;
	} else if(*p == '"') {
	    quote ^= 1;
	    p++;
	    continue;
	}
	/* csh uses the !quote test; I don't */
	if(home && /* !quote && */ *p == '/') {
	    *d = 0;
	    d = getopt?of->pbuf:of->pbuf+strlen(of->pbuf)+1;
	    d = homesubst(d);
	    home = 0;
	}
	if(*p == '$') {
	    p++;
	    s = d;
	    if(*p == '{') {
		while(*++p && *p != '}')
		  *d++ = *p;
		p++;
	    } else
	      while(*p && isalnum(*p))
		*d++ = *p++;
	    *d = 0;
	    if((d = getenv(s)))
	      strcpy(s,d);
	    else
	      *s = 0;
	    d = s+strlen(s);
	    continue;
	}
	if(!quote && isspace(*p))
	  break;
	if(*p == '\\' && p[1])
	  p++;
	*d++ = *p++;
    }
    *d = 0;
    d = getopt?of->pbuf:of->pbuf+strlen(of->pbuf)+1;
    if(home)
      homesubst(d);
    if(*p)
      while(isspace(*++p));
    if(debug & DB_OPT)
      fprintf(stderr,"optfile: '%.*s' -> '%s'\n",(int)(p-of->bptr),of->bptr,d);
    of->bptr = p;
    return d;
}

static void optfile(const char *n)
{
    FILE *f = fopen(n,"r");
    struct ofile *of = NULL; /* init to keep GCC happy */

    if(!f || !(of = malloc(sizeof(*of))))
      help(1);
    of->f = f;
    of->bptr = of->buf;
    of->buf[0] = 0;
    parseopts(readfile,of);
}

struct prargs {
    int argc;
    char **argv;
};

static const char *getarg(void *data, int getopt)
{
    struct prargs *args = data;
    if(!--args->argc)
      return NULL;
    else
      return *++args->argv;
}

void parseargs(int argc, char **argv)
{
    struct prargs ar;

    ar.argc = argc;
    ar.argv = argv;
    parseopts(getarg,&ar);
}

static void endarg(void)
{
    struct op *op;

    if(defop.srcls.site.host) {
	*nxtop = op = malloc(sizeof(*op));
	if(!op)
	  help(1);
	nxtop = &op->next;
	*op = defop;
	/* fix up self-references */
	if(op->srcls.dirs == &defop.srcls.dir0)
	  op->srcls.dirs = &op->srcls.dir0;
	if(op->dstls.dirs == &defop.dstls.dir0)
	  op->dstls.dirs = &op->dstls.dir0;
	/* all counts & arrays are 0, so just share fname & strmem space */
	op->dstls.dirm = op->srcls.dirm;
	/* mangle defaults in */
	if(op->srcls.type == ST_NONE)
	  op->srcls.type = ST_FTPD;
	if(op->dstls.type == ST_NONE)
	  op->dstls.type = ST_MFILE;
	if(op->dstls.type == ST_MFILE && !op->dstls.lsfile)
	  op->dstls.lsfile = lsbase?lsbase:DEF_LSFILE;
	if(op->srcls.type == ST_MFILE && !op->srcls.lsfile)
	  op->srcls.lsfile = lsbase?lsbase:DEF_LSFILE;
	if(!op->dstls.site.host)
	  op->dstls.site.host = op->srcls.site.host;
	if(!op->dstls.dir0) {
	    op->dstls.dir0 = op->srcls.dir0;
	    op->dstls.dirs = op->srcls.dirs;
	    op->dstls.ndirs = op->srcls.ndirs;
	}
	/* reset for next pass */
	memset(&defop.srcls, 0, sizeof(defop.srcls));
	/* save timeouts */
	defop.srcls.site.to = defop.dstls.site.to;
	memset(&defop.dstls, 0, sizeof(defop.dstls));
	/* save timeouts */
	defop.dstls.site.to = defop.srcls.site.to;
	defop.srcls.type = defop.dstls.type = 0;
	defop.dstf_remt = defop.srcf_local = 0;
	defop.trymir = 0;
    }
}

/* minor parse helper macros */

#define copyparm(s) copystr(s,strlen(s),&defop.srcls.dirm.names)
#define copyparml(s,l) copystr(s,l,&defop.srcls.dirm.names)

#define optstrm(parm) do { \
    if(!(s = (*getarg)(data,0))) \
      help(1); \
    if(parm) \
      free((void *)parm); \
    if(*s) { \
	parm = malloc(strlen(s)+1); \
	if(!parm) \
	  help(1); \
	strcpy((char *)parm,s); \
    } else \
      parm = NULL; \
} while(0)

#define optstr(parm) do { \
    if(!(s = (*getarg)(data,0))) \
      help(1); \
    if(*s) { \
	parm = copyparm(s); \
	if(!parm) \
	  help(1); \
    } else \
      parm = NULL; \
} while(0)

#define optint(parm) do {\
    if(!(s = (*getarg)(data,0))) \
      help(1); \
    parm = atoi(s); \
} while(0)

#define setfilt(filt) do {\
    if(debug & DB_OPT) \
	fprintf(stderr,"filt: %s\n", defop.filt##filter); \
    if((s=break_lp(defop.filt##filter, &defop.filt##regex, &defop.filt##re_array, &defop.filt##nre))) { \
	fprintf(stderr,"Can't compile %s: %s\n",defop.filt##filter,s); \
	defop.filt##filter = NULL; \
	help(1); \
    } \
} while(0)

#define filtfile(filt) do { \
    if(!(s = (*getarg)(data,0))) \
      help(1); \
    if(stat(s, &st)) { \
	fprintf(stderr, "Invalid filter %s\n", s); \
	help(1); \
    } \
    defop.filt##filter = malloc(st.st_size + 1); \
    if(!defop.filt##filter || !(i=open(s,O_RDONLY)) || \
       read(i,(void *)defop.filt##filter,st.st_size)!=st.st_size || close(i)) { \
	fprintf(stderr, "Invalid filter %s\n", s); \
	help(1); \
    } \
    p=(char *)defop.filt##filter + st.st_size - 1; \
    p[1] = 0; \
    for(i = st.st_size;i && *p == '\n' ; i--, p--) \
      *p = 0; \
    for(;i;i--,p--) { \
	if(*p == '\n') \
	  *p = '|'; \
    } \
    if(!*defop.filt##filter) \
      defop.filt##filter = NULL; \
    else \
      setfilt(filt); \
} while(0)

/* minor parse helper functions for literal args */

/* strips one more layer of \'s off & parses out acct. & passwd */
/* return: NULL or location of a directory */
static const char *parsesite(const char *_s, struct ftpsite *site)
{
    const char *s = _s;
    char *d, *sp = NULL, *ap = NULL, *pp = NULL, *dp = NULL, *port = NULL;
    char pasv = 0;

    for(d = scratch; *s; s++, d++) {
	if(*s == '\\' && s[1]) {
	    *d = *++s;
	    continue;
	}
	if(!sp && *s == '@') {
	    *d = 0;
	    sp = d+1;
	    continue;
	}
	if(!sp && !pp && !pasv && *s == '!') {
	    *d = 0;
	    pasv = 1;
	    continue;
	}
	if(!sp && !pp) {
	    if(*s == ':') {
		*d = 0;
		pp = d+1;
		continue;
	    }
	    if(!ap && *s == ',') {
		*d = 0;
		ap = d+1;
		continue;
	    }
	}
	if(sp && !dp) {
	    if(!port && *s == ',') {
		port = d+1;
		*d = 0;
		continue;
	    }
	    if(*s == ':') {
		*d = 0;
		dp = d+1;
		continue;
	    }
	}
	*d = *s;
    }
    *d = 0;
    if(sp) {
	if(*sp)
	  site->host = copyparm(sp);
	if(ap && (!pp || !*scratch)) {
	    fprintf(stderr,"Warning: acct %s w/o user & passwd in %s ignored\n",
		    ap, _s);
	    ap = NULL;
	}
	site->user = *scratch?copyparm(scratch):NULL;
	site->acct = ap?copyparm(ap):NULL;
	site->pass = pp?copyparm(pp):anonpasswd?copyparm(anonpasswd):NULL;
	site->port = port?atoi(port):0; /* FIXME: getportbyname() */
	site->passive = pasv || defop.defpassive;
    } else {
	if(*scratch)
	  site->host = copyparm(scratch);
	site->user = NULL;
	site->acct = NULL;
	site->pass = anonpasswd?copyparm(anonpasswd):NULL;
	site->port = ap?atoi(ap):0; /* FIXME: getportbyname() */
	site->passive = pasv || defop.defpassive;
	dp = pp;
    }
    return dp;
}

static void litarg(const char *arg)
{
    if(!defop.srcls.site.host)
      arg = parsesite(arg, &defop.srcls.site);
    if(arg) {
	arg = copyparm(arg);
	if(defop.srcls.ndirs) {
	    if(defop.dstls.type == ST_FTPD) {
		fputs("There must be a one-one remote/local dir correspondence\n",stderr);
		help(1);
	    }
	    if(defop.srcls.ndirs == 1) {
		defop.srcls.dirs = malloc(sizeof(char *)*5);
		defop.srcls.dirs[0] = defop.srcls.dir0;
	    }
	    else if(!(defop.srcls.ndirs%5))
	      defop.srcls.dirs = realloc(defop.srcls.dirs,
					 sizeof(char *)*5*
					 (defop.srcls.ndirs/5+1));
	    defop.srcls.dirs[defop.srcls.ndirs] = arg;
	    defop.srcls.ndirs++;
	} else {
	    defop.srcls.dir0 = arg;
	    defop.srcls.dirs = &defop.srcls.dir0;
	    defop.srcls.ndirs++;
	}
    }
}

static void setdir(struct ftpdir *dir, const char *set)
{
    const char *loc = strchr(set, ':');

    if(!loc++ || !*loc) {
	fprintf(stderr,"%s: bad location specifier\n", set);
	help(1);
    }
    switch(set[0]) {
      case 'l':
	dir->lsfile = copyparm(loc);
	dir->type = ST_LSLRF;
	return;
      case 'c':
	dir->lsfile = copyparm(loc);
	dir->type = ST_CMD;
	return;
      case 'm':
	dir->lsfile = copyparm(loc);
	dir->type = ST_MFILE;
	return;
      case 'd':
	dir->type = ST_LDIR;
	break;
      case 'f':
	dir->type = ST_FTPD;
	set = parsesite(loc, &dir->site);
	break;
      case 'L':
	dir->type = ST_FTPLSLRF;
	loc = parsesite(loc, &dir->site);
	if(!loc)
	{
	    fprintf(stderr, "%s: bad location specifier\n", set);
	    help(1);
	}
	dir->lsfile = copyparm(loc);
	return;
      default:
	fprintf(stderr,"%s: bad location specifier\n", set);
	help(1);
    }
    /* this here's only for 'd'/'f' */
    if(!dir->ndirs)
      dir->dirs = &dir->dir0;
    else {
	if(dir->ndirs == 1) {
	    dir->dirs = malloc(sizeof(char *)*5);
	    dir->dirs[0] = dir->dir0;
	} else if(!(dir->ndirs % 5))
	    dir->dirs = realloc(dir->dirs, sizeof(char *)*5*(dir->ndirs/5+1));
    }
    dir->dirs[dir->ndirs++] = set;
}

static void parseopts(const char *(*getarg)(void *, int), void *data)
{
    const char *a, *s = NULL; /* s init to keep GCC quiet */
    char *p = NULL; /* p init to keep GCC quiet */
    struct stat st;
    int i;
    char tog;
    static int subnest = 0;
    int infirst = !subnest;

    subnest = 1;
    if(infirst) {
	defop.srcls.site.to = defop.dstls.site.to = defto;
#ifdef DEF_CRPG
	crpg = malloc(strlen(DEF_CRPG)+1);
	strcpy(crpg, DEF_CRPG);
#endif
    }

    while((a = (*getarg)(data,1))) {
	if(*a == '-' || *a == '+') {
	    tog = *a == '-';
	    while(*++a) {
		switch(*a) {
		  case 'F': /* site file */
		    if(!(s = (*getarg)(data,0)))
		      help(1);
		    optfile(s);
		    break;
		  case 'B': /* run in background */
		    bg = 1;
		    break;
		  case 'q': /* suppress report */
		    defop.quiet = tog;
		    break;
		  case 'P': /* fork */
		    defop.dofork = tog;
		    break;
		  case 'N': /* don't continue if error */
		    defop.dep = tog;
		    break;
		  case 'p': /* anonymous password */
		    optstrm(anonpasswd);
		    break;
		  case 'b': /* base for ls files */
		    optstrm(lsbase);
		    break;
		  case 'z': /* compressor program */
		    optstrm(crpg);
		    break;
		  case 'Z': /* uncompressor program */
		    optstrm(uncrpg);
		    break;
		  case 'U': /* detect unchanged files */
		    defop.detmove = tog;
		    break;
		  case 'M': /* force manual recursion */
		    defop.manrecurs = tog;
		    break;
		  case 'L': /* remote ls file (next site only) */
		    if(defop.srcls.type != ST_NONE)
		      fputs("Warning: -f overrides -L\n", stderr);
		    optstr(defop.srcls.lsfile);
		    defop.srcls.type = ST_FTPLSLRF;
		    break;
		  case 'm': /* mirror */
		    defop.domirop = tog;
		    break;
		  case 'd': /* local dir */
		    if(defop.trymir && defop.dstf_remt)
		      fputs("Warning: -d overrides -t\n", stderr);
		    optstr(defop.dstls.dir0);
		    defop.dstls.dirs = &defop.dstls.dir0;
		    defop.dstls.ndirs = 1;
		    defop.dstls.type = ST_LDIR;
		    defop.dstls.linkout = defop.deflinkout;
		    defop.trymir = 1;
		    defop.dstf_remt = 0;
		    break;
		  case 'e': /* execute command for each file */
		    if(defop.trymir && defop.dstf_remt)
		      fputs("Warning: -e overrides -t\n", stderr);
		    optstr(defop.dstls.pipe);
		    defop.trymir = 1;
		    break;
		  case 't':
		    defop.dstls.type = ST_FTPD;
		    defop.dstls.dir0 = parsesite(s, &defop.dstls.site);
		    defop.dstls.dirs = &defop.dstls.dir0;
		    defop.dstls.ndirs = 1;
		    defop.trymir = 1;
		    defop.dstf_remt = 1;
		    break;
		  case 'c':
		    defop.srcf_local = tog;
		    break;
		  case 'g':
		    optstr(defop.srcls.pipe);
		    break;
		  case 'l': /* mirror from listfile */
		    optstr(defop.dstls.lsfile);
		    defop.dstls.type = ST_MFILE;
		    break;
		  case 'f': /* use prev. generated listfile for remt ls */
		    if(defop.srcls.type != ST_NONE)
		      fputs("Warning: -f overrides -L\n", stderr);
		    optstr(defop.srcls.lsfile);
		    defop.srcls.type = ST_MFILE;
		    break;
		  case 'o':
		    if(!(s = (*getarg)(data,0)))
		      help(1);
		    setdir(&defop.srcls, s);
		    break;
		  case 'O':
		    if(!(s = (*getarg)(data,0)))
		      help(1);
		    setdir(&defop.dstls, s);
		    if(defop.trymir && (defop.dstls.type == ST_FTPD ||
					defop.dstls.type == ST_LDIR))
		      fputs("Warning: -O f/d overrides -d/-t\n", stderr);
		    break;
		  case 'E':
		    defop.mirmodes = tog;
		    break;
		  case 'n':
		    defop.mirstmp = tog;
		    break;
		  case 'A':
		    defop.mirappend = tog;
		    break;
		  case 'i': /* filter */
		    optstr(defop.ifilter);
		    if(defop.ifilter)
		      setfilt(i);
		    break;
		  case 'I': /* filter (from file) */
		    filtfile(i);
		    break;
		  case 'x': /* filter */
		    optstr(defop.xfilter);
		    if(defop.xfilter)
		      setfilt(x);
		    break;
		  case 'X': /* filter (from file) */
		    filtfile(x);
		    break;
		  case 'D': /* filter out all directories */
		    defop.filtdir = tog;
		    break;
		  case 's': /* don't filter out all specials */
		    defop.filtspec = tog^1;
		    break;
		  case 'S':
		    defop.deflinkout = tog;
		  case 'V':
		    defop.defpassive = tog;
		    break;
		  case 'T': /* timeouts */
		    if(!(s = (*getarg)(data,0)))
		      help(1);
		    while(*s) {
			if(!isdigit(s[1])) {
			    fprintf(stderr,"Bad timeout specifier %s\n",s);
			    help(1);
			}
			switch(*s++) {
			  case 't': /* transfer */
			    defop.srcls.site.to.xfer = atoi(s);
			    break;
			  case 'c': /* connect */
			    defop.srcls.site.to.connect = atoi(s);
			    break;
			  case 'q': /* quit */
			    defop.srcls.site.to.quit = atoi(s);
			    break;
			  case 'r': /* retry # */
			    defop.srcls.site.to.retrycnt = atoi(s);
			    break;
			  case 'd': /* retry delay */
			    defop.srcls.site.to.retrytime = atoi(s);
			    break;
			  case 'o': /* "operations" (commands w/o xfer) */
			    defop.srcls.site.to.cmd = atoi(s);
			    break;
			  default:
			    fprintf(stderr,"Bad timeout specifier %s\n",s-1);
			    help(1);
			}
			while(isdigit(*s))
			  s++;
		    }
		    break;
		  case 'R':
		    optstrm(reptpipe);
		    break;
		  case 'r':
		    if(!(s = (*getarg)(data,0)))
		      help(1);
		    switch(*s++) {
		      case 't': /* std report title */
			defop.rhead = copyparm(s);
			break;
		      case 'd': /* std report line */
			defop.rfmt = copyparm(s);
			break;
		      case 'f': /* std report footer */
			defop.rtail = copyparm(s);
			break;
		      case 's': /* std erport sort */
			defop.rsort = parsesort(s);
			break;
		      case 'T': /* mir report title */
			defop.ehead = copyparm(s);
			break;
		      case 'D': /* mir report line */
			defop.efmt = copyparm(s);
			break;
		      case 'F': /* mir report footer */
			defop.etail = copyparm(s);
			break;
		      case 'S': /* mir report sort */
			defop.esort = parsesort(s);
			break;
		      case 'e': /* error line (title) */
			defop.errln = copyparm(s);
			break;
		      default:
			fprintf(stderr,"Bad report string specifier %s\n",s);
			help(1);
		    }
		    break;
		  case 'v': /* debug level */
		    optint(debug);
		    if(debug)
		      /* perhaps linebuf would be better, but this is easier */
		      setbuf(stdout, NULL);
		    break;
		  case '-': /* next arg literal */
		    if(!(s = (*getarg)(data,0)))
		      help(1);
		    litarg(s);
		    break;
		  case '+': /* end of arg list */
		    endarg();
		    break;
		  case 'h': /* help */
		    help(0);
		  default:
		    /* Unused: ajkuwy CGHJKQSWY */
		    help(1);
		}
	    }
	} else
	  litarg(a);
    }
    endarg();
    if(infirst) {
	subnest = 0;
	if(!ops)
	    help(0);
    }
}
