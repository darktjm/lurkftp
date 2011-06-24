/*
 * lurkftp - lurk around FTP sites, possibly grabbing things
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

#include "ftpsupt.h"
#define PROGRAMNAME "lurkftp v1.00"

#define DEF_LSFILE ".chkls.%s%d.gz"

/* DEF_CRPG can be commented out for no default */
#define DEF_CRPG "gzip"
#define DEF_UNCRPG "gunzip"


/*
 * Set this to 1 to enable experimental time correction code.
 * This may end up being needed for mirroring sites that report time
 * incorrectly either with LIST (which should provide local time) or
 * with MDTM (which should provide UTC).
 * The time-correction code was written when I discovered that all the date
 * stamps on my old debian mirror were screwed up.  I decided not to add it
 * after all when I discovered that the reason for this mismatch was a bug
 * in ncftp (used by my old mirror script), not lurkftp.
 */
#define TIMECORRECT 1

/* default report line format */
/* #define DEFFMT "%T %{%Y %m %d} %12b %r%f%[L? -> %L]" */
#define DEFFMT "%T %d %12b %r%f%[l? -> %L]"

/* default report headers */
#define DEFHEAD "--- %s %d ---"
#define DEFEHEAD "\n*** ERRORS IN %S %P -> %s %p MIRROR ***"
#define DEFTAIL "%t"
#define DEFETAIL "\n"

/* -------------------- end of user-configurable items ------------------*/

#define DB_LOCK  8
#define DB_TRACE 16
#define DB_OPT   32
#define DB_OTHER 64

enum ls_type {
    ST_NONE = 0,
    ST_FTPD,	 /* remote LIST [site+dirs] */
    ST_LDIR,	 /* local readdir [dirs] */
    ST_MFILE,	 /* placemarker [lsfile] */
    ST_LSLRF,	 /* local ls-lR file [lsfile] */
    ST_FTPLSLRF, /* remote ls-lR file [site+lsfile] */
    ST_CMD	 /* local command producing ls-lR-type output [lsfile] */
};

struct ftpdir {
    struct ftpsite site;
    struct dirmem dirm;
    const char *lsfile;
    const char *pipe;
    const char **dirs;
    const char *dir0;
    unsigned short ndirs;
    unsigned char type;
#ifdef TIMECORRECT
    signed char timecorrect: 2;
#endif
    unsigned char linkout: 1;
};

struct op {
    struct op *next;
    struct ftpdir srcls, dstls;
    const char *rhead, *rfmt, *rtail;
    const char *ehead, *efmt, *etail;
    unsigned rsort, esort;
    const char *errln;
    const char *xfilter, *ifilter;
    regex_t xregex, iregex;
    regex_t *xre_array, *ire_array;
    int xnre, inre;
    unsigned int
      srcf_local: 1,
      dstf_remt: 1,
      filtdir: 1,
      filtspec: 1, /* default: 1 */
      quiet: 1,
      dep: 1,
      dofork: 1,
      detmove: 1,
      manrecurs: 1,
      domirop: 1,
      trymir: 1,
      mirmodes: 1,
      mirstmp: 1,
      mirappend: 1,
      defpassive : 1,
      deflinkout : 1;
};

struct totals {
    unsigned long addb, subb, addd, subd, addf, subf, adds, subs;
};

/* lurkftp.c */
extern char *progname;

extern char *reptpipe;

extern int bg;

extern char scratch[4096];

void unlockof(void);
void lockof(void);
extern FILE *of;

/* mir.c */
void mir_chmod(struct ftpdir *dst, struct fname **ap, int nadd);
void mir_del(struct ftpdir *dst, struct fname **dp, int ndel, char filtdir);
void mir_move(struct ftpdir *dst, struct fname **mirarray, int malen,
	      int *nadd, int *ndel);
void mir_transfer(struct fname **ap, int nadd, struct ftpdir *src,
		  const struct ftpdir *dst, char mirstmp, char mirmodes,
		  char mirappend);
void mir_errrpt(struct fname **ap, struct fname **dp, int nadd,
		int ndel, const char *header, const char *tail,
		const struct ftpdir *site, const struct ftpdir *other,
		const char *fmt);

/* opt.c */
extern struct op *ops;
extern int nops;
void parseargs(int argc, char ** argv);

/* diff.c */
/*
 * sort for reports & default diff:
 * order by type, then
 * year, month, day, name, [lnname], size, [min/sec]
 */
#define REPTCMP  0x7654320 /* "fdpnlst" */

#define DIFFCMP  0x7265430 /* "fpnlsdt" */
/*
 * sort for move detection:
 * type, name (w/o dir), [linkname], date, size, dir: ascending
 */
#define MVCMP 0x7362540 /* "fnldspt" */

/*
 * diff-compare for move detection:
 * same as sorter above but no dir name difference
 */
#define MVCHK 0x762540 /* "fnldst" */

/*
 * sort for mirror processing:
 * name, [linkname], type, date, size: descending
 * [only files care about date & size]
 */
#define PROCCMP 0xfea8dcb /* "PNLFDST" */

extern unsigned sorder;
extern signed char tc1, tc2;
unsigned parsesort(const char *str);
int gencmp(const void *__a, const void *__b);
#define sort(a,n,o) do {sorder = o; tc1=tc2=0; qsort(a,n,sizeof(*a),gencmp);}while(0)
unsigned strip_from_sorder(unsigned sorder, unsigned char what);
struct fname **diff_dir(struct ftpdir *src, struct ftpdir *dst,
			int *arraylen, int *nadd, int *ndel,
			char detmove, char chkmodes, char appendmode);

/* rept.c */
void headsubst(char *buf, const char *pat, const struct ftpdir *site,
	       const struct ftpdir *other, const struct totals *tot);
void namesubst(char *buf, const char *pat, const struct ftpsite *src,
	       const struct ftpdir *dst, const struct fname *item, char type);
void pr_rept(struct fname **mirarray, int malen, int nadd, int ndel,
	     const char *header, const char *tail, const struct ftpdir *site,
	     const struct ftpdir *other, const char *fmt, unsigned sort,
	     char resort);

/* misc.c */
#define allocmem(l) allocstr(l,&dir->names)
#define copyname(s) copystr(s,strlen(s),&dir->names)
#define copynamel(s,l) copystr(s,l,&dir->names)
void freedir(struct dirmem *dir);
char *allocstr(int len, struct strmem **sp);
char *copystr(const char *str, int len, struct strmem **sp);
struct fname *lastname(struct fnamemem **pd);
void freelastname(struct fnamemem **pd);
int toarray(struct dirmem *dir);
time_t gmt_mktime(struct tm *tm);

int readtree(struct dirmem *dir, const char *wd, char linkout);
typedef int (*read_func)(void *data, char *buf, int buflen, char line);
int readf(FILE *f, char *buf, int len, char line);
int parsels(struct dirmem *dir, read_func rf, void *data,
	    const char *cd, const char *root);
void filterdir(struct dirmem *dir, const regex_t *filter, int nfilter, int cut);

int readlsfile(struct dirmem *dir, FILE *f, const char *root);
int writelsfile(const char *name, struct dirmem *dir, int mustproc);

size_t nosig_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t nosig_fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream);

/* routines in ftpsupt.c */
int getfile(FILE *f, struct ftpsite *site, const char *rf);
const char *ftperr(int err);
int readftplsf(struct dirmem *dir, struct ftpsite *site, const char *rname);
int readftpdir(struct dirmem *dir, struct ftpsite *site, const char **dirs,
	       int ndirs, int recurse, const regex_t *xfilt, int nxfilt);

/* break_lp */
const char *break_lp(const char *txt, regex_t *re0, regex_t **_re_array, int *_nre);
int match_lp(const char *txt, const regex_t *re_array, int nre);
void free_lp(regex_t **_re_array, int *_nre);

/* glibc2.1 support */
#if defined(SEMCTL_NEEDS_ARG) && defined(__GNU_LIBRARY__) && \
    defined(__GLIBC__) && defined(__GLIBC_MINOR__) && \
     ((__GLIBC__ == 2 && __GLIBC_MINOR__ > 0) || __GLIBC__ > 2)
#undef SEMCTL_NEEDS_ARG
#endif
