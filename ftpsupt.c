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
#include <setjmp.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pwd.h>

/* some generic scratch buffers that prevent this */
/* from ever being thread-safe */
static char buf[4096];
static char name[4096];

#define ret_lo(x) do{ftp_closeconn(site);return (x);}while(0)

#define copyname(s) copystr(s,strlen(s),&dir->names)

/* read remote dir */
static int doftpdir(struct dirmem *dir, struct ftpsite *site,
		    const char **dirs, int ndirs, int recurse,
		    const regex_t *xfilt)
{      
    char *s;
    const char *cd;
    unsigned long i, j;
    int noparm = 0;
    struct fnamemem *fm = NULL; /* initialized to satisfy GCC */
    struct fname *f;
    int res;

    if((res = ftp_openconn(site)) != ERR_OK)
      ret_lo(res);
    if((res = ftp_type(site, "A")) != ERR_OK)
      ret_lo(res);
    if(dir->count) {
	f = lastname(&dir->files);
	do {
	    s = buf;
	    if(**dirs != '/') {
		if(ftp_getwd(site, buf) != ERR_OK)
		  ret_lo(ERR_PWD);
		s = buf+strlen(buf);
		if(s == buf || s[-1] != '/')
		  *s++ = '/';
	    }
	    strcpy(s,*dirs);
	    s += strlen(s);
	    if(s == buf || s[-1] != '/') {
		*s++ = '/';
		*s = 0;
	    }
	} while(strcmp(f->root, buf) && dirs++ && --ndirs);
	cd = f->root;
    } else
      cd = NULL;
    for(;ndirs--;dirs++, cd = NULL) {
	if(!cd) {
	    s = buf;
	    if(recurse > 1)
	      recurse -= 2;
	    if(**dirs != '/') {
		if(ftp_getwd(site, buf) != ERR_OK)
		  ret_lo(ERR_PWD);
		s = buf+strlen(buf);
		if(s == buf || s[-1] != '/')
		  *s++ = '/';
	    }
	    strcpy(s,*dirs);
	    s += strlen(s);
	    if(s == buf || s[-1] != '/') {
		*s++ = '/';
		*s = 0;
	    }
	    cd = copyname(buf);
	    if(!cd)
	      ret_lo(ERR_MEM);
	} else
	    recurse += 2;
	if(!recurse) {
	    /* first try single recursive list command */
	    strcpy(buf,cd);
	    /* strip trailing / for command */
	    if(buf[1])
	      buf[strlen(buf)-1] = 0;
	    if((i = ftp_port(site)) == ERR_OK &&
	       (i=ftp_cmd2(site, "LIST -lRa ", buf)) == ERR_OK &&
	       (i=parsels(dir,(read_func)ftp_read,site,cd,cd)) == ERR_OK &&
	       dir->count > 1) {
		/* recover already-read dirs under the assumption */
		/* that this recovery is faster than re-FTPing the dir */
		const char *lastdir, *curdir = NULL;

		res = ftp_endport(site);
		/* need to reread lastdir if endport returns an error */
		if(res == ERR_OK)
		  lastdir = NULL;
		else {
		    f = lastname(&dir->files);
		    lastdir = f->dir;
		}
		if(debug & DB_TRACE)
		  fprintf(stderr, "Restarting at %s\n", lastdir? lastdir: "skipped");
		fm=dir->files;
		f=&fm->names[NSKIP-1];
		curdir = f->dir; /* skip root directory */
		for(i = 0; i < dir->count; i++, f--) {
		    if(i && !(i % NSKIP)) {
			fm = fm->nxt;
			f = &fm->names[NSKIP-1];
		    }
		    if(f->dir == lastdir)
		      break;
		    if(f->dir != curdir) {
			struct fnamemem *fm2;
			struct fname *f2;

			curdir = f->dir;
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
				if(debug & DB_TRACE)
				  fprintf(stderr, "Closing %s%s\n", f2->dir, f2->name);
				f2->flags &= ~FNF_DODIR;
				recurse--;
				break;
			    }
			}
		    }
		    if(f->flags & FNF_DODIR) {
			if(debug & DB_TRACE)
			  fprintf(stderr, "Checking out %s%s\n", f->dir, f->name);
			recurse++;
		    }
		}
		/* remove [partial] directory read */
		if(lastdir) {
		    while(dir->count > 0 &&
			  (f = lastname(&dir->files)) &&
			  f->dir == curdir) {
			freelastname(&dir->files);
			dir->count--;
		    }
		    ret_lo(res);
		}
		/* at this point, I could ret_lo(ERR_DIRR), but maybe the */
		/* skipped dirs really are empty, so read recursively */
		if(recurse) /* more dirs left */
		  recurse = 2;
	    }
	}
	/* special hack for non-dirs */
	/* this will force an error if non-dir isn't a symlink to a dir */
	if(!recurse && i == ERR_OK && dir->count == 1 &&
	   (f = lastname(&dir->files)) && /* should always be true */
	   !S_ISDIR(f->mode) &&
	   !strncmp(f->dir,f->name,strlen(f->dir)-1)) {
	    recurse = 1;
	    /* remove the non-dir from listing */
	    freelastname(&dir->files);
	    dir->count--;
	}
	/* try manual recursion if recursive list failed */
	if(recurse || i || !dir->count) {
	    const char *root = cd;

	    i = -1;
	    f = NULL;
	    while(1) {
		/* find new dir to cd to */
		while(++i<dir->count) {
		    if(!f) {
			for(j=i/NSKIP,fm=dir->files;j;j--)
			  fm = fm->nxt;
			f = &fm->names[NSKIP-1-i%NSKIP];
		    } else if(!(i % NSKIP)) {
			fm = fm->nxt;
			f = &fm->names[NSKIP-1];
		    } else
		      f--;
		    if(f->flags & FNF_DODIR) {
			strcpy(buf,f->dir);
			strcat(buf,f->name);
			s = buf+strlen(buf);
			if(s[-1] != '/') {
			    *s++ = '/';
			    *s = 0;
			}
			if(!xfilt || regexec(xfilt, buf, 0, NULL, 0)) {
			    cd = copyname(buf);
			    if(!cd)
			      ret_lo(ERR_MEM);
			    break;
			}
		    }
			  
		}
		if(i && i >= dir->count)
		  break;
		/* strip trailing / for command */
		strcpy(buf,cd);
		if(buf[1])
		  buf[strlen(buf)-1] = 0;
		if((j = ftp_chdir(site, buf)) == ERR_OK) {
		    if(noparm || (j = ftp_port(site)) != ERR_OK ||
		       (j = ftp_cmd(site,"list -a")) != ERR_OK) {
			noparm = 1;
			if((j = ftp_port(site)) == ERR_OK)
			  j = ftp_cmd(site,"list");
		    }
		    if(j == ERR_OK) {
			if((j=parsels(dir,(read_func)ftp_read,site,cd,root)) != ERR_OK)
			  ret_lo(j);
			if((j=ftp_endport(site)) != ERR_OK) {
			    /* remove [partial] directory read */
			    if(dir->count > 0) {
				f = lastname(&dir->files);
				if(!strcmp(f->dir, cd)) {
				    cd = f->dir;
				    while(dir->count > 0 &&
					  (f = lastname(&dir->files)) &&
					  f->dir == cd) {
					freelastname(&dir->files);
					dir->count--;
				    }
				}
				ret_lo(res);
			    }
			    ret_lo(j);
			}
		    } else
		      ret_lo(j);
		} else {
		    lockof();
		    fprintf(of, "*** %s: Skipping %s: %s\n", site->host, buf, site->lastresp);
		    unlockof();
		}
		/* the next stuff is in case server doesn't take parameters */
		/* for list, but doesn't report an error */
		if(!noparm && !dir->count)
		    noparm = 1;
		/* at this point, the read must have been "successful", so */
		/* just mark it as done even if it was empty */
		else if(f)
		  f->flags &= ~FNF_DODIR;
	    }
	}
    }
    i = toarray(dir);
    if(i)
      ret_lo(i);
    return i;
}

/* read remote dir w/ retry */
int readftpdir(struct dirmem *dir, struct ftpsite *site, const char **dirs,
	       int ndirs, int recurse, const regex_t *xfilt)
{
    int i,err = 0;
    struct timeval rt;

    for(i=site->to.retrycnt?site->to.retrycnt:-1;i;i--) {
	if(debug & DB_TRACE)
	  fprintf(stderr,"Trying %s [#%d]\n",site->host,site->to.retrycnt-i+1);
	if((err=doftpdir(dir, site, dirs, ndirs, recurse, xfilt)) && err != ERR_DIR) {
	    if(debug & DB_TRACE)
	      fprintf(stderr,"%s; waiting %d seconds\n",ftperr(err),site->to.retrytime);
	    rt.tv_sec = site->to.retrytime;
	    rt.tv_usec = 0;
	    select(0,NULL,NULL,NULL,&rt);
	} else
	  break;
    }
    /* return specific error even when max retries */
    /* this is so report shows exact error */
    return !err && !i ? ERR_MAXRETR : err;
}

/* read remote dir from remote file w/ retry */
/* FIXME: this should read directly instead of using temp file */
int readftplsf(struct dirmem *dir, struct ftpsite *site, const char *dirs,
	       const char *rname)
{
    FILE *f;
    int err;
    char *s;

    strcpy(name,dirs);
    if((s = strchr(name,' ')))
      *s = 0;
    else
      s = name+strlen(name);
    if(s == name || s[-1] != '/') {
	*s++ = '/';
	*s = 0;
    }
    if(!(s = copyname(name)))
      return ERR_MEM;
    if(!(f = tmpfile())) {
	perror("tmp file for ftplsf");
	return ERR_OS;
    }
    if((err = getfile(f,site,rname))) {
	fclose(f);
	return err;
    }
    rewind(f);
    /* root may be inaccurate if dirs contains more than one dir, but */
    /* root is only used by mirroring, which guarantees only one dir */
    err = parsels(dir,(read_func)readf,f,s,s);
    fclose(f);
    if(err == ERR_OK)
      err = toarray(dir);
    if(err)
      ret_lo(err);
    return err;
}

/* retrieve a file: log in if necessary, try restart if f has data */
/* log off if error, else keep connection open */
/* return: 0 == ftp failure; -1 == write failure; 1 = success */
static int retrfile(FILE *f, struct ftpsite *site, const char *rf)
{
    long n;
    int ret;

    if((ret = ftp_openconn(site)) != ERR_OK)
      ret_lo(ret);
    if((ret = ftp_type(site, "I")) != ERR_OK)
      ret_lo(ret);
    if((ret = ftp_port(site)) != ERR_OK)
      ret_lo(ret);
    if((n = ftell(f)) > 0) {
	sprintf(buf,"%ld",n);
	if(ftp_cmd2(site, "REST ", buf) != ERR_OK)
	  rewind(f);
    }
    if((ret = ftp_cmd2(site, "RETR ", rf)) != ERR_OK && ret != ERR_CMD)
      ret_lo(ret);
    n = 0;
    do {
	if(n && nosig_fwrite(buf,n,1,f) != 1) {
	    perror("fwrite");
	    ret_lo(ERR_OS);
	}
    } while((n=ftp_read(site,buf,4096,0)) > 0);
    if(n<0)
      ret_lo((int)-n);
    return ftp_endport(site); /* don't log out for next file */
}

/* get a file with retry */
int getfile(FILE *f, struct ftpsite *site, const char *fname)
{
    int i,err;
    struct timeval rt;

    for(err=0,i=site->to.retrycnt?site->to.retrycnt:-1;i;i--) {
	/* retrfile will set f to correct file position */
	if((err=retrfile(f, site, fname)) && (err != ERR_OS || errno == EINTR) && err != ERR_CMD) {
	    if(fflush(f) && errno != EINTR) {
		err = ERR_OS;
		break;
	    }
	    if(debug & DB_TRACE)
	      fprintf(stderr,"%s; waiting %d seconds\n",ftperr(err),site->to.retrytime);
	    rt.tv_sec = site->to.retrytime;
	    rt.tv_usec = 0;
	    select(0,NULL,NULL,NULL,&rt);
	} else
	  break;
    }
    /* always return MAXRETR if max retries reached */
    /* no specific error will be reported, anyway */
    /* return !i?ERR_MAXRETR:err; */
    return !i && !err?ERR_MAXRETR:err;
}

/* misc functions */
const char *ftperr(int err)
{
    switch(err) {
      case ERR_OK:
	return "No error";
      case ERR_EOF:
	return "End of File";
      case ERR_TO:
	return "Timed out";
      case ERR_MEM:
	return "Out of memory";
      case ERR_DIR:
	return "Nothing in directory";
      case ERR_LOGIN:
	return "Can't log in";
      case ERR_PWD:
	return "Can't get current working directory";
      case ERR_OS:
	return "OS error";
      case ERR_CMD:
	return "Error sending command";
      case ERR_MAXRETR:
	return "Maximum retry count reached";
    }
    return "Unknown error";
}
