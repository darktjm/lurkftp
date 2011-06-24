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

#include "lurkftp.h"
#include <sys/wait.h>
#include <utime.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int debug;

char *progname;
char *curstat;

char *reptpipe = NULL;

int bg = 0;

static int anyfork = 0;

FILE *of;
static int oflock = -1; /* semaphore */

char scratch[4096]; /* temp. buffer */

/* fork() support: output file locking */
void lockof(void)
{
    struct sembuf sop;

    if(oflock < 0)
      return;
    if(debug & DB_LOCK)
      fputs("Attempting to lock output file...\n",stderr);
    sop.sem_num = 0;
    sop.sem_op = -1;
    sop.sem_flg = 0; /* maybe SEM_UNDO */
    errno = 0;
    while(semop(oflock,&sop,1) < 0 && errno == EAGAIN) errno = 0;
    if(errno)
      perror("semop");
    if(debug & DB_LOCK)
      fputs("Output file locked\n",stderr);
}

void unlockof(void)
{
    struct sembuf sop;

    if(oflock < 0)
      return;
    fflush(of);
    sop.sem_num = 0;
    sop.sem_op = 1;
    sop.sem_flg = 0;
    semop(oflock,&sop,1);
    if(debug & DB_LOCK)
      fputs("Output file unlocked\n",stderr);
}

static void rmoflock(void)
{
#ifdef _GNU_SOURCE
    union semun arg;
#endif

    if(anyfork) {
#ifdef _GNU_SOURCE
	arg.val = 0;
	semctl(oflock,0,IPC_RMID,arg);
#else
	semctl(oflock,0,IPC_RMID);
#endif
    }
}

static int getoflock(void)
{
    if(oflock >= 0)
      return 1;
    oflock = semget(IPC_PRIVATE,1,IPC_CREAT|S_IRWXU);
    if(oflock < 0)
      return 0;
    atexit(rmoflock);
    unlockof();
    return 1;
}

/* fork() support: kill all children */
static void killchld(void)
{
    if(anyfork) {
	signal(SIGTERM,SIG_IGN);
	kill(-getpgrp(),SIGTERM);
    }
}

static int file_read(FILE *f, void *buf, int buflen)
{
    return fread(buf, buflen, 1, f);
}

static int read_dir(struct ftpdir *dir, char manrecurs, regex_t *xfilt)
{
    FILE *lf;
    int err;

    switch(dir->type) {
      case ST_FTPD:
	return readftpdir(&dir->dirm, &dir->site, dir->dirs, dir->ndirs,
			  manrecurs, xfilt);
      case ST_LDIR:
	return readtree(&dir->dirm,dir->dir0);
      case ST_MFILE:
	headsubst(scratch,dir->lsfile,dir,NULL,NULL);
	if((lf = fopen(scratch, "r"))) {
	    err = readlsfile(&dir->dirm, lf, dir->dir0);
	    fclose(lf);
	    return err;
	} else {
	    lockof();
	    fprintf(of,"*** %s doesn't exist! ***\n",scratch);
	    unlockof();
	    return ERR_DIR;
	}
      case ST_LSLRF:
	headsubst(scratch,dir->lsfile,dir,NULL,NULL);
	if((lf = fopen(scratch, "r"))) {
	    err = parsels(&dir->dirm, (read_func)readf, lf, dir->dir0, dir->dir0);
	    fclose(lf);
	    return err;
	} else {
	    lockof();
	    fprintf(of,"*** %s doesn't exist! ***\n",scratch);
	    unlockof();
	    return ERR_DIR;
	}
      case ST_FTPLSLRF:
	return readftplsf(&dir->dirm, &dir->site, dir->dir0, dir->lsfile);
      case ST_CMD:
	headsubst(scratch,dir->lsfile,dir,NULL,NULL);
	if((lf = popen(scratch, "r"))) {
	    err = parsels(&dir->dirm, (read_func)file_read, lf, dir->dir0,
			  dir->dir0);
	    pclose(lf);
	    return err;
	} else {
	    lockof();
	    fprintf(of,"*** Can't execute %s! ***\n",scratch);
	    unlockof();
	    return ERR_OS;
	}
    }
    return 0;
}


void filterdir(struct dirmem *dir, const regex_t *filter, int cut)
{
    struct fname **p, **q;
    int i, cnt = dir->count;

    for(p=q=dir->array,i=dir->count;i--;p++) {
	strcpy(scratch,(*p)->dir);
	strcat(scratch,(*p)->name);
	if(S_ISLNK((*p)->mode)) {
	    strcat(scratch," -> ");
	    strcat(scratch,(*p)->lnk);
	}
	if(!regexec(filter, scratch, 0, NULL, 0) == !cut)
	  *q++ = *p;
	else {
	    if(debug & DB_OTHER)
	      fprintf(stderr,"Filtering out %s\n",scratch);
	    cnt--;
	}
    }
    dir->count = cnt;
}
static void filt_dir(struct dirmem *dir, const char *ifilter, regex_t *iregex,
		     const char *xfilter, regex_t *xregex,
		     char filtdir, char filtspec)
{
    struct fname **o, **n;
    int i, j;

    if(ifilter) {
	if(debug & DB_TRACE)
	  fprintf(stderr,"Filtering out all but '%s'\n",ifilter);
	filterdir(dir, iregex, 0);
    }
    if(xfilter) {
	if(debug & DB_TRACE)
	  fprintf(stderr,"Filtering out '%s'\n",xfilter);
	filterdir(dir, xregex, 1);
    }
    if(filtdir) {
	for(o=n=dir->array,j=0,i=dir->count;i;i--,o++)
	  if(!S_ISDIR((*o)->mode)) {
	      *n++ = *o;
	      j++;
	  }
	dir->count = j;
	}
	if(filtspec) {
	    for(o=n=dir->array,j=0,i=dir->count;i;i--,o++)
	      if(!S_ISSOCK((*o)->mode) && !S_ISCHR((*n)->mode) &&
		 !S_ISBLK((*o)->mode) && !S_ISFIFO((*n)->mode)) {
		  *n++ = *o;
		  j++;
	      }
	    dir->count = j;
	}
}

#if TIMECORRECT
void timecorrect(struct ftpdir *dir)
{
    /* time-correct */
    if(op->srcls.type == ST_FTPD && op->srcls.dirm.count) {
	/* since this list sorted in date-first ascending order, get last */
	/* (i.e. most recent) entry & see if it matches file's MDTM date */
	for(i=op->srcls.dirm.count,n=op->srcls.dirm.array+i-1;i;i--,n--)
	  if(S_ISREG((*n)->mode)) /* only files have important dates */
	    break;
	if(i) { /* if there was any file at all, n points to most recent */
	    strcpy(scratch,(*n)->dir);
	    strcat(scratch,(*n)->name);
	    if(filetm(site,scratch,&tm) && /* mdtm command successful */
	       (tm.tm_year != (*n)->year-1900 || tm.tm_mon != (*n)->month-1 ||
		tm.tm_mday != (*n)->day ||
		((*n)->hr >= 0 && tm.tm_hour != (*n)->hr) ||
		((*n)->min >= 0 && tm.tm_min != (*n)->min))) {
		time_t t1, t2;
		/* ugh! remote ls returns inaccurate times! */
		/* see how inaccurate it is.. */
		t1 = mktime(&tm);
		tm.tm_year = (*n)->year-1900;
		tm.tm_mon = (*n)->month-1;
		tm.tm_mday = (*n)->day;
		if((*n)->hr >= 0)
		  tm.tm_hour = (*n)->hr;
		else
		  tm.tm_hour = 12; /* why not? */
		if((*n)->min >= 0)
		  tm.tm_min = (*n)->min;
		else
		  tm.tm_min = 0;
		tm.tm_sec = 0;
		tm.tm_isdst = -1;
		t2 = mktime(&tm);
		i = (time_t)difftime(t1,t2);
		if(debug & DB_TRACE) {
		    j = i<0?-i:i;
		    fprintf(stderr,"Correcting time by %s%d:%d:%d:%d\n",
			    i<0?"-":"",j/(24*60*60),j/(60*60)%24,j/60%60,j%60);
		}
		for(i=op->srcls.dirm.count,n=op->srcls.dirm.array;i;i--,n++) {
		    tm.tm_year = (*n)->year-1900;
		    tm.tm_mon = (*n)->month-1;
		    tm.tm_mday = (*n)->day;
		    if((*n)->hr >= 0)
		      tm.tm_hour = (*n)->hr;
		    else
		      tm.tm_hour = 12; /* why not? */
		    if((*n)->min >= 0)
		      tm.tm_min = (*n)->min;
		    else
		      tm.tm_min = 0;
		    tm.tm_sec = 0;
		    t1 = mktime(&tm) + i;
		    tm = *localtime(&t1);
		    (*n)->year = tm.tm_year+1900;
		    (*n)->month = tm.tm_mon+1;
		    (*n)->day = tm.tm_mday;
		    if((*n)->hr >= 0)
		      (*n)->hr = tm.tm_hour;
		    if((*n)->min >= 0)
		      (*n)->min = tm.tm_min;
		}
	    }
	}
    }
}
#endif

static void process_all(void)
{
    struct op *op;
    int i;
    pid_t pid = -1;
    struct fname **mirarray;
    int ndel, nadd, malen;
    int domir;
    char *s;

    if(ops)
      ops->dep = 0; /* can't depend on nothing */
    for(op = ops;op;op = op->next) {
	/* in case of continue when forked, quit */
	if(!pid && !op->dep)
	  exit(0);

	/* set up main listing file */
	if(!of && (!reptpipe || !(of = tmpfile())))
	  of = stdout;

	/* clean up zombie processes */
	if(pid && oflock >= 0)
	  while((i = waitpid(-1,NULL,WNOHANG)) > 0 || (i && errno != ECHILD));
	errno = 0;

	/* prepare for fork */
	if(op->dofork && !op->dep && getoflock()) {
	    if((pid = fork()) > 0) { /* parent */
		anyfork = 1;
		while(op->next && op->next->dep)
		  op = op->next;
		continue;
	    }
	    if(!pid) { /* child */
		anyfork = 0; /* no children */
		if(debug & DB_TRACE)
		  fprintf(stderr,"Forked pid %ld\n",(unsigned long)getpid());
		if(debug & DB_OTHER) /* give gdb time to attach */
		  sleep(5);
	    }
	    /* here, it's either the child or no child was created */
	}

	/* perform actual processing: */
	strcpy(curstat, op->srcls.site.host);
	for(s = curstat+strlen(curstat); *s || s[1]; s++)
	  *s = 0;
	
	domir = op->trymir && op->domirop;

	/* read "remote" dir */
	if(debug & DB_TRACE)
	  fputs("Reading remote directory\n",stderr);
	i = read_dir(&op->srcls, op->manrecurs, op->xfilter?&op->xregex:NULL);
	if(i) {
	    if(!op->quiet) {
		lockof();
		fprintf(of,"*** %s %s: %s ***\n",op->srcls.site.host,
			op->srcls.dir0, ftperr(i));
		unlockof();
	    }
	    continue;
	}
	/* run filters on it */
	filt_dir(&op->srcls.dirm, op->ifilter, &op->iregex,
		 op->xfilter, &op->xregex, op->filtdir, op->filtspec);
	/* check if anything left after filtering */
	if(!op->srcls.dirm.count) {
	    lockof();
	    fprintf(of,"*** %s %s: Nothing left after filter ***\n",
		    op->srcls.site.host,op->srcls.dir0);
	    unlockof();
	    continue;
	}
	/* sort; ready for diffing. */
	sort((const struct fname **)op->srcls.dirm.array, op->srcls.dirm.count,
	     op->detmove?MVCMP:REPTCMP);
#if TIMECORRECT
	if(op->srcls.type != ST_LDIR && op->srcls.type != ST_MFILE)
	  timecorrect(&op->srcls);
#endif
	/* read "local" dir */
	if(debug & DB_TRACE)
	  fputs("Reading local directory\n",stderr);
	i = read_dir(&op->dstls,op->manrecurs,
		 op->xfilter?&op->xregex:NULL);
	if(i && i != ERR_DIR) {
	    fprintf(stderr,"Internal error: %s\n", ftperr(i));
	    continue;
	}
	/* sort; ready for diffing. */
	sort((const struct fname **)op->dstls.dirm.array, op->dstls.dirm.count,
	     op->detmove?MVCMP:REPTCMP);
#if TIMECORRECT
	if(op->dstls.type != ST_LDIR && op->dstls.type != ST_MFILE)
	  timecorrect(&op->dstls);
#endif
	/* diff listings */
	mirarray = diff_dir(&op->srcls, &op->dstls, &malen, &nadd, &ndel,
			    op->detmove, op->mirmodes && domir,
			    op->mirappend && !op->dstls.pipe);
	/* report differences */
	if(debug & DB_TRACE)
	  fputs("Printing report\n",stderr);
	if(!op->quiet && (nadd || ndel))
	  pr_rept(mirarray, malen, nadd, ndel, op->rhead?op->rhead:DEFHEAD,
		  op->rtail?op->rtail:DEFTAIL, &op->srcls, &op->dstls,
		  op->rfmt?op->rfmt:(op->trymir? /* or maybe domir? */
				     (op->filtdir?"%[d:" DEFFMT "]":DEFFMT):
				     (op->filtdir?
				      "%[dT<T>TM:" DEFFMT "]":
				      "%[T<T>TM:" DEFFMT "]")),
		  op->rsort?op->rsort:REPTCMP, op->rsort || op->detmove);
	/* now perform mirroring functions */
	if(domir) {
	    if(op->mirmodes && nadd)
	      mir_chmod(&op->dstls, mirarray, nadd);
	    if(!op->mirstmp && ndel)
	      mir_del(&op->dstls, mirarray+malen-ndel,ndel, op->filtdir);
	    if(op->detmove && ndel && nadd)
	      mir_move(&op->dstls, mirarray, malen, &nadd, &ndel);
	    if(nadd)
 	      mir_transfer(mirarray, nadd, &op->srcls, &op->dstls,
			   op->mirstmp, op->mirmodes, op->mirappend);
	    mir_errrpt(mirarray, mirarray+malen-ndel, nadd, ndel,
		       op->ehead?op->ehead:DEFEHEAD,
		       op->etail?op->etail:DEFETAIL, &op->dstls, &op->srcls,
		       op->filtdir?"%[d:" DEFFMT "]":DEFFMT);
	}
	/* write out new list file */
	/* deferred until here so that failed transfers aren't added */
	if((ndel || nadd) && op->dstls.type == ST_MFILE) {
	    headsubst(scratch,op->dstls.lsfile,&op->dstls,NULL,NULL);
	    if((i = writelsfile(scratch,&op->srcls.dirm,domir)))
	      if(!op->quiet) {
		  lockof();
		  fprintf(of,"*** %s %s: %s ***\n",op->srcls.site.host,
			  op->srcls.dir0, ftperr(i));
		  unlockof();
	      }
	}
	/* reset variables between site/dir lists */
	ftp_closeconn(&op->srcls.site); /* in case file or dir retrieval was in progress */
	ftp_closeconn(&op->dstls.site); /* in case file or dir retrieval was in progress */
	/* in case we got here when forked, quit */
	if(!pid && (!op->next || !op->next->dep))
	  exit(0);
	freedir(&op->srcls.dirm); /* dstls on same dirm */
	if(mirarray) {
	    free(mirarray);
	    mirarray = NULL;
	}
    }
    if(!pid)
      exit(0);
}

static void cleanexit(int ret)
{
    FILE *f;
    int i;

    /* wait for all children */
    if(oflock >= 0)
      while(waitpid(-1,NULL,0) > 0 || errno != ECHILD);
    anyfork = 0;

    /* pipe out the report */
    if(reptpipe && of != stdout && ftell(of) > 0) {
	f = popen(reptpipe,"w");
	rewind(of);
	while((i = nosig_fread(scratch, 1, 2048, of)) > 0)
	  nosig_fwrite(scratch,i,1,f);
	pclose(f);
    }
    if(of != stdout)
      fclose(of);

    exit(ret);
}

void ignsig(int ign)
{
}

int main(int argc, char **argv)
{
    time_t tm;
    pid_t pid;
    struct sigaction sa;

    ftp_init(argv[0]);

    progname = argv[0];
    curstat = argv[1];

    /* for dir listings -- mainly current year/month */
    time(&tm);
    memcpy(&curtm,localtime(&tm),sizeof(curtm));

    atexit(killchld);
    sigaction(SIGCHLD, NULL, &sa);
    sa.sa_handler = ignsig;
    sa.sa_flags |= SA_RESTART|SA_NOCLDSTOP;
    sa.sa_flags &= ~SA_RESETHAND;
    sigaction(SIGCHLD, &sa, NULL);

    parseargs(argc, argv); /* parse all args */

    if(bg) { /* dissociate if requested */
	freopen("/dev/null","w",stdout);
	fclose(stdin);
	freopen("/dev/null","w",stderr);
	pid = fork();
	if(pid > 0) {
	    anyfork = 0;
	    exit(0);
	} else if(!pid)
	    setsid();
    }

    process_all();

    cleanexit(0);

    return 0;
}
