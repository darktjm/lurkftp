/*
 * Support routines for lurkftp
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
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pwd.h>

char *crpg = NULL;
char *uncrpg = NULL;

struct tm curtm; /* to be filled in by main() */

/* some generic scratch buffers that prevent this */
/* from ever being thread-safe */
static char buf[4096];
static char name[4096];

/* memory functions */

#define copyname(s) copystr(s,strlen(s),&dir->names)
#define copynamel(s,l) copystr(s,l,&dir->names)

char *allocstr(int len, struct strmem **sp)
{
    struct strmem *sm;
    char *s;

    while((sm = *sp)) {
	sp = &sm->nxt;
	if(BUFLEN-1-sm->ptr > (unsigned)len) {
	    s = sm->buf+sm->ptr;
	    sm->ptr += len+1;
	    return s;
	}
    }
    sm = *sp = malloc(sizeof(**sp));
    if(!sm)
      return NULL;
    sm->nxt = NULL;
    sm->ptr = len+1;
    return sm->buf;
}

char *copystr(const char *str, int len, struct strmem **sp)
{
    char *s = allocstr(len, sp);

    if(s) {
	memcpy(s,str,len);
	s[len] = 0;
    }
    return s;
}

static struct fname *allocname (struct fnamemem **pd)
{
    struct fnamemem *dir;
    struct fname *f;

    while((dir = *pd)) {
	pd = &dir->nxt;
	/* this assumes size will be set immediately to non-~0 */
	if(dir->names[0].size == ~0UL) {
	    f = &dir->names[dir->names[0].year--];
	    f->flags = 0;
	    return f;
	}
    }
    dir = *pd = malloc(sizeof(**pd));
    if(!dir)
      return NULL;
    dir->nxt = NULL;
    dir->names[0].size = ~0;
    dir->names[0].year = NSKIP-2;
    f = &dir->names[NSKIP-1];
    f->flags = 0;
    return f;
}

void freelastname(struct fnamemem **pd)
{
    struct fnamemem *dir = NULL;

    while(*pd) {
	dir = *pd;
	/* this assumes size will be set immediately to non-~0 */
	if(dir->names[0].size == ~0UL) {
	    dir->names[0].year++;
	    if(dir->names[0].year == NSKIP - 1) {
		free(dir);
		*pd = NULL;
	    }
	    return;
	}
	pd = &dir->nxt;
    }
    if(!dir) /* error, actually */
      return;
    dir->names[0].size = ~0UL;
    dir->names[0].year = 0;
}

struct fname *lastname(struct fnamemem **pd)
{
    struct fnamemem *dir = NULL;

    while(*pd) {
	dir = *pd;
	pd = &dir->nxt;
	/* this assumes size will be set immediately to non-~0 */
	if(dir->names[0].size == ~0UL)
	  return &dir->names[dir->names[0].year+1];
    }
    if(!dir) /* error, actually */
      return NULL;
    return &dir->names[0];
}

void freedir(struct dirmem *dir)
{
    struct fnamemem *fm, *ft;
    struct strmem *sm, *st;
    
    for(fm=dir->files;fm;fm=ft) {
	ft = fm->nxt;
	free(fm);
    }
    dir->files = NULL;
    for(sm=dir->names;sm;sm=st) {
	st = sm->nxt;
	free(sm);
    }
    dir->names = NULL;
    dir->count = 0;
    if(dir->array)
      free(dir->array);
    dir->array = NULL;
}

int toarray(struct dirmem *dir)
{
    int i,m,c = dir->count;
    struct fname **p, *q;
    struct fnamemem *fm = dir->files;

    if(!c)
      return ERR_DIR;
    p = dir->array = malloc(sizeof(*dir->array)*dir->count);
    if(!p) {
	freedir(dir);
	return ERR_MEM;
    }
    while(fm && c--) {
	m = fm->names[0].size == ~0UL?fm->names[0].year:-1;
	for(i=NSKIP-1,q=fm->names+i;i>m;i--)
	  *p++ = q--;
	fm = fm->nxt;
    }
    return ERR_OK;
}

/* parse functions */

int readf(FILE *f, char *buf, int len, char line)
{
    if(line) {
	const char *s = fgets(buf, len, f);

	if(!s && feof(f))
	  return ERR_EOF;
	else if(!s)
	  return ERR_OS;
	else
	  return ERR_OK;
    } else
      return fread(buf, 1, len, f);
}

/* I only check 1st character for binary; this should suffice for most */
/* compression formats & it keeps me from having to use a magic->prog table */
/* if fork() or exec() fails, then this'll return nothing */
FILE *autouncompress(read_func f, void *fd, pid_t *pp, char *fc)
{
    int p[2], i, r;
    char c;
    FILE *pf;

    if(f(fd, fc, 1, 0) != 1)
      return NULL;
    if(isprint(*fc) || isspace(*fc))
      return NULL;
    c = *fc;
    if((i = pipe(p)) || (*pp = fork()) < 0) {
	perror("uncompress pipe");
	while(f(fd, buf, 4096, 0) > 0);
	if(i) {
	    close(p[0]);
	    close(p[1]);
	}
	return fopen("/dev/null", "r");
    }
    if(!*pp) {
	close(p[0]);
	fclose(stdout);
	close(1); /* just in case fclose() didn't */
	dup2(p[1],1);
	close(p[1]);
	pf = popen(uncrpg?uncrpg:DEF_UNCRPG, "w");
	if(!pf) {
	    fprintf(stderr,"Can't execute uncompressor \"%s\"\n",uncrpg?uncrpg:DEF_UNCRPG);
	    exit(-1);
	}
	fputc(c, pf);
	while((i = f(fd, buf, 4096, 0)) > 0) {
	    if((i = nosig_fwrite(buf, i, 1, pf)) != 1)
	      break;
	}
	r = pclose(pf);
	fprintf(stderr, "Done writing to uncompressor %s\n", uncrpg?uncrpg:DEF_UNCRPG);
	if(i < 0 || r < 0) {
	    perror("uncompress");
	    exit(1);
	}
	exit(0);
    }
    close(p[1]);
    pf = fdopen(p[0], "r");
    return pf;
}

/* reap and/or kill child */
void reap(pid_t p)
{
    if(!p)
      return;
    errno = 0;
    while(waitpid(p, NULL, 0) != p && errno != ECHILD) kill(p, SIGPIPE);
}

int readlsfile(struct dirmem *dir, FILE *f, const char *root)
{
    char *s, ch;
    FILE *f2;
    const char *cd = "./";
    short y, m, d, hr, min;
    unsigned int md;
    unsigned long sz;
    struct fname *cur;
    pid_t pid;

    f2 = autouncompress((read_func)readf, f, &pid, &ch);
    if(!f2)
      ungetc(ch, f);
    else
      f = f2;
    /* FIXME: use manual scan instead */
    while(fscanf(f, "%hd %hd %hd %hd:%hd %o %ld %[^\n]\n", &y, &m, &d, &hr,
		 &min, &md, &sz, name) == 8) {
	cur = allocname(&dir->files);
	if(!cur) {
	    reap(pid);
	    return ERR_MEM;
	}
	dir->count++;
	cur->year = y;
	cur->month = m;
	cur->day = d;
	cur->hr = hr;
	cur->min = min;
	cur->mode = md;
	cur->size = sz;
	if(S_ISLNK(md) && (s = strstr(name," -> "))) { /* soft link */
	    *s = 0;
	    cur->lnk = copyname(s+4);
	    if(!cur->lnk) {
		reap(pid);
		return ERR_MEM;
	    }
	}
	if((s = strrchr(name,'/'))) {
	    s++;
	    cur->name = copyname(s);
	    if(!cur->name) {
		reap(pid);
		return ERR_MEM;
	    }
	    if(strlen(cd) != (unsigned)(s-name) || strncmp(cd,name,s-name)) {
		cd = copynamel(name,s-name);
		if(!cd) {
		    reap(pid);
		    return ERR_MEM;
		}
	    }
	} else {
	    cd = "./";
	    cur->name = copyname(name);
	    if(!cur->name) {
		reap(pid);
		return ERR_MEM;
	    }
	}
	cur->dir = cd;
	cur->root = root;
    }
    reap(pid);
    return toarray(dir);
}

int writelsfile(const char *name, struct dirmem *dir, int mustproc)
{
    struct fname **n;
    int i;
    FILE *f, *pf = NULL;

    if(crpg) {
	strcpy(buf, crpg);
	strcat(buf, " > ");
	strcat(buf, name);
	if(!(pf = f = popen(buf, "w")))
	  f = fopen(name, "w");
    } else
      f = fopen(name, "w");
    if(!f)
      return ERR_OS;
    for(n=dir->array,i=dir->count;i;i--,n++)
      if((!mustproc || !((*n)->flags & FNF_PROCESS) ||
	  ((*n)->flags & FNF_DONE)) &&
	 fprintf(f, "%4hd %2hd %02hd %02hd:%02hd %7o %12ld %s%s%s%s\n",
		 (*n)->year, (*n)->month, (*n)->day, (*n)->hr, (*n)->min,
		 (unsigned)(*n)->mode, (*n)->size, (*n)->dir, (*n)->name,
		 S_ISLNK((*n)->mode)?" -> ":"",
		 S_ISLNK((*n)->mode)?(*n)->lnk:"") <= 0)
	break;
    if(i || (pf ? pclose(f):fclose(f)))
      return ERR_OS;
    return ERR_OK;
}

static char *skipfld(char *src)
{
    while(*src && *src != ' ' && *src != '\t') src++;
    if(*src)
      *src++ = 0;
    while(*src == ' ' || *src == '\t') src++;
    return src;
}

/* parse results of ls-lR from remote site */
int parsels(struct dirmem *dir, read_func rf, void *data, const char *cd,
	    const char *root)
{
    int i, y, hr, min;
    struct tm tm;
    mode_t m;
    char *s, *t, *curdir, ch;
    struct fname *fn;
    int totx = 0;
    pid_t pid;
    FILE *f;

    /* note: compression makes timeouts less accurate, but who cares? */
    f = autouncompress(rf, data, &pid, &ch);
    if(f) {
	rf = (read_func)readf;
	data = f;
    }
    else
      buf[0] = ch;
    if(cd) {
	i = strlen(cd);
	if(cd[i-1] != '/') {
	    curdir = copynamel(cd,i+1);
	    if(!curdir) {
		reap(pid);
		return ERR_MEM;
	    }
	    curdir[i] = '/';
	    curdir[i+1] = 0;
	} else {
	    curdir = copynamel(cd,i);
	    if(!curdir) {
		reap(pid);
		return ERR_MEM;
	    }
	}
    } else {
	curdir = copynamel("",0);
	if(!curdir) {
	    reap(pid);
	    return ERR_MEM;
	}
    }
    if(debug & DB_TRACE)
      fprintf(stderr,"Reading directory %s\n",curdir);
    while((*rf)(data,buf+(f?1:0),4096-(f?1:0),1) == ERR_OK) {
	f = NULL;
	totx += strlen(buf);
	if(debug & DB_OTHER)
	  fprintf(stderr,"Read: %s", buf);
	if(sscanf(buf,"total %d",&i) == 1)
	  continue;
	i = strlen(buf)-1;
	while(isspace(buf[i])) i--;
	buf[i+1] = 0;
	s = name;
	if(*curdir == '/' && buf[0] != '/')
	  *s++ = '/';
	if(buf[i] == ':' && sscanf(buf,"%[^ \t:]:",s) == 1) {
	    i = strlen(name);
	    if(name[i-1] != '/')
	      name[i++] = '/';
	    /* mark the previous dir as read */
	    if(dir->count) {
		struct fnamemem *fm2;
		struct fname *f2;
		unsigned long j;

		if(debug & DB_TRACE)
		  fprintf(stderr, "Closing directory %s\n", curdir);
		/* I wish there was a better way to do this... */
		/* perhaps store as real tree; future enhancement */
		fm2 = dir->files;
		f2 = &fm2->names[NSKIP-1];
		for(j=0; j < dir->count; j++, f2--) {
		    if(j && !(j % NSKIP)) {
			fm2 = fm2->nxt;
			f2 = &fm2->names[NSKIP-1];
		    }
		    if(f2->flags & FNF_DODIR &&
		       !strncmp(f2->dir, curdir, strlen(f2->dir)) &&
		       !strncmp(f2->name, curdir+strlen(f2->dir), strlen(f2->name)) &&
		       curdir[strlen(f2->dir)+strlen(f2->name)+1] == 0) {
			f2->flags &= ~FNF_DODIR;
			break;
		    }
		}
	    }
	    curdir = copynamel(name,i);
	    if(!curdir) {
		reap(pid);
		return ERR_MEM;
	    }
	    if(debug & DB_TRACE)
	      fprintf(stderr,"Reading directory %s\n",curdir);
	    continue;
	}
	/* parse file type */
	switch(buf[0]) {
	  case 'd':
	    m = S_IFDIR;
	    break;
	  case 'l':
	    m = S_IFLNK;
	    break;
	  case 'c':
	    m = S_IFCHR;
	    break;
	  case 'b':
	    m = S_IFBLK;
	    break;
	  case 's':
	    m = S_IFSOCK;
	    break;
	  case 'p':
	    m = S_IFIFO;
	    break;
	  default:
	    m = S_IFREG;
	    break;
	}
	for(i=1,y=0;i<=9;y+=3) {
	    if(buf[i++] == 'r')
	      m += S_IRUSR>>y;
	    if(buf[i++] == 'w')
	      m += S_IWUSR>>y;
	    switch(buf[i++]) {
	      case 'x':
		m += S_IXUSR>>y;
		break;
	      case 's':
		m += S_IXUSR>>y;
	      case 'S':
		if(i == 3)
		  m += S_ISUID;
		else if(i == 6)
		  m += S_ISGID;
		break;
	      case 't':
		m += S_IXUSR>>y;
	      case 'T':
		if(i==9)
		  m += S_ISVTX;
		break;
	    }
	}
	s = skipfld(buf); /* protection */
	s = skipfld(s); /* # of links */
	s = skipfld(s); /* user */
	/* should just check here if group or not, but what the hell... */
	/* skip any other intervening fields until size + month/date */
	i = 0; /* major/minor dev# */
	do {
	    if(S_ISBLK(m) || S_ISCHR(m)) {
		i = (i&0xffff)<<16;
		i |= atoi(s)&0xffff; /* hopefully large enough */
		s = skipfld(s);
	    } else
	      i = atoi(s);
	    s = skipfld(s); /* size or minor dev# */
	    t = s;
	    if(!isdigit(*t)) {
		s = strptime(t,"%b",&tm);
		if(!s)
		  s = t;
	    }
	} while(*s && s == t);
	if(!*s)
	  continue;
	s = skipfld(s); /* month */
	t = s;
	s = skipfld(s); /* day */
	/* For older files exact time should be reflected in size */
	if(!strchr(s,':')) {
	    y = atoi(s);
	    hr = min = -1;
	} else {
	    y = curtm.tm_year+1900;
	    if(tm.tm_mon > curtm.tm_mon)
	      y--;
	    hr = atoi(s);
	    min = atoi(strchr(s,':')+1);
	}
	s = skipfld(s);
	/* skip if . or .. */
	if(S_ISDIR(m) && s[0] == '.' && (!s[1] || (s[1] == '.' && !s[2])))
	  continue;
	fn = allocname(&dir->files);
	if(!fn) {
	    reap(pid);
	    return ERR_MEM;
	}
	fn->dir = curdir;
	fn->root = root;
	fn->mode = m;
	if(S_ISDIR(m))
	  fn->flags = FNF_DODIR;
	fn->size = i;
	fn->year = y;
	fn->month = tm.tm_mon+1;
	fn->day = atoi(t);
	fn->hr = hr;
	fn->min = min;
	if(S_ISLNK(m)) {
	    if((t = strstr(s," -> "))) { /* soft link */
		*t = 0;
		fn->lnk = copyname(t+4);
		if(!fn->lnk) {
		    reap(pid);
		    return ERR_MEM;
		}
	    } else /* don't know link contents */
	      fn->lnk = "~*UNKNOWN_LINK*~";
	}
	fn->name = copyname(s);
	if(!fn->name) {
	    reap(pid);
	    return ERR_MEM;
	}
	if(debug & DB_OTHER)
	  fprintf(stderr,"Created: %02d:%02d %02d/%02d/%04d (%ld/%o) %s%s%s%s\n",
		  fn->hr,fn->min,fn->month,fn->day,fn->year,
		  fn->size,(unsigned)fn->mode,fn->dir,fn->name,
		  S_ISLNK(fn->mode)?" -> ":"",
		  S_ISLNK(fn->mode)?fn->lnk:"");
	dir->count++;
    }
    alarm(0);
    reap(pid);
    return ERR_OK;
}

/* descend local directory tree for file list */
/* Can't use ftw because ftw's brain-dead */
int readtree(struct dirmem *dir, const char *_wd)
{
    struct stat st;
    struct fname *f, *dirf;
    struct fnamemem *fm = NULL;
    struct tm *tm;
    char *root;
    int wdlen;
    unsigned long i;
    DIR *d;
    struct dirent *de;
    char *wd;

    if(!*_wd)
      _wd = "./";
    wdlen = strlen(_wd);
    if(_wd[wdlen-1] != '/') {
	wdlen++;
	wd = copynamel(_wd,wdlen);
	if(wd) {
	    wd[wdlen-1] = '/';
	    wd[wdlen] = 0;
	}
    } else
      wd = copyname(_wd);
    if(!wd)
      return ERR_MEM;
    root = wd;
    dirf = NULL;
    i = dir->count - 1;
    do {
	/* first, read in dir */
	strcpy(name,wd);
	wdlen = strlen(name);
	if(wdlen > 1)
	  wd[wdlen-1] = 0; /* strip trailing '/' for opendir */
	d = opendir(wd);
	if(wdlen > 1)
	  wd[wdlen-1] = '/'; /* replace trailing '/' */
	if(!d)
	    fprintf(stderr,"Can't scan directory %s\n",wd);
	else {
	    while((de = readdir(d))) {
		/* skip "." and ".." */
		if(de->d_name[0] == '.' && (!de->d_name[1] ||
					    (de->d_name[1] == '.' &&
					     !de->d_name[2])))
		  continue;
		strcpy(name+wdlen,de->d_name);
		if(lstat(name,&st)) {
		    fprintf(stderr,"Can't stat %s\n",name);
		    continue;
		}
		if(!(f=allocname(&dir->files)))
		  return ERR_MEM;
		f->mode = st.st_mode;
		if(S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode))
		  f->size = (major(st.st_rdev)<<16)+minor(st.st_rdev);
		else
		  f->size = st.st_size;
		tm = gmtime(&st.st_mtime);
		f->year = tm->tm_year+1900;
		f->month = tm->tm_mon+1;
		f->day = tm->tm_mday;
		f->hr = tm->tm_hour;
		f->min = tm->tm_min;
		if(!(f->name = copyname(de->d_name)))
		  return ERR_MEM;
		f->root = root;
		f->dir = wd;
		/* read link contents */
		if(S_ISLNK(st.st_mode)) {
		    int len = readlink(name,buf,4096);

		    if(len < 0) {
			perror(name);
			return ERR_OS;
		    }
		    buf[len] = 0;
		    if(!(f->lnk = copyname(buf)))
		      return ERR_MEM;
		}
		if(debug & DB_OTHER)
		  fprintf(stderr,"Created: %02d:%02d %02d/%02d/%04d (%ld/%o) %s%s%s%s\n",
			  f->hr,f->min,f->month,f->day,f->year,
			  f->size,(unsigned)f->mode,f->dir,f->name,
			  S_ISLNK(f->mode)?" -> ":"",
			  S_ISLNK(f->mode)?f->lnk:"");
		dir->count++;
	    }
	    closedir(d);
	}
	/* then, find new dir to cd to */
	while(++i<dir->count) {
	    if(!dirf) {
		for(wdlen=i/NSKIP,fm=dir->files;wdlen;wdlen--)
		  fm = fm->nxt;
		dirf = &fm->names[NSKIP-1-i%NSKIP];
	    } else if(!(i % NSKIP)) {
		fm = fm->nxt;
		dirf = &fm->names[NSKIP-1];
	    } else
	      dirf--;
	    if(S_ISDIR(dirf->mode)) {
		strcpy(name,dirf->dir);
		strcat(name,dirf->name);
		wd = name+strlen(name);
		if(wd[-1] != '/') {
		    *wd++ = '/';
		    *wd = 0;
		}
		wd = copyname(name);
		if(!wd)
		  return ERR_MEM;
		wdlen = strlen(wd);
		break;
	    }
	}
    } while(i < dir->count);
    return toarray(dir);
}

/* signal-immune versions of fread() & fwrite() */
size_t nosig_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t nread = 0, n;
    size_t ntoread = nmemb * size;
    
    while((n = fread((char *)ptr + nread, 1, ntoread, stream)) < ntoread &&
	  (errno == EAGAIN || errno == EINTR)) {
	nread += n;
	ntoread -= n;
    }
    nread += n;
    ntoread -= n;
    return nread / size;
}

size_t nosig_fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t nwrite = 0, n;
    size_t ntowrite = nmemb * size;
    
    while((n = fwrite((char *)ptr + nwrite, 1, ntowrite, stream)) < ntowrite &&
	  (errno == EAGAIN || errno == EINTR)) {
	nwrite += n;
	ntowrite -= n;
    }
    nwrite += n;
    ntowrite -= n;
    return nwrite / size;
}
