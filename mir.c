/*
 * Mirroring functions for lurkftp
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
#include <utime.h>
#include <sys/wait.h>

/* return: 0 = error; else success */
/* assumes name begins with '/' */
static int mkpath(char *dir)
{
    struct stat st;
    char *s;

    s = strrchr(dir,'/');
    if(!s[1])
      *s = 0;
    if(!stat(dir,&st)) {
	if(!s[1])
	  *s = '/';
	return S_ISDIR(st.st_mode);
    }
    if(!s[1]) {
	*s = '/';
	while(*--s != '/' && s > dir);
    }
    while(s > dir) {
	*s = 0;
	if(!stat(dir,&st)) {
	    *s = '/';
	    if(!S_ISDIR(st.st_mode))
	      return 0;
	    break;
	}
	*s = '/';
	while(*--s != '/' && s > dir);
    }
    while((s = strchr(s+1,'/'))) {
	*s = 0;
	if(mkdir(dir, 0777)) {
	    *s = '/';
	    return 0;
	}
	*s = '/';
	if(!s[1])
	  return 1;
    }
    /* only gets here for last element in path */
    return !mkdir(dir, 0777);
}

/* FIXME: using wrong name on moved files, most likely */
void mir_chmod(struct ftpdir *dst, struct fname **ap, int nadd)
{
    char *s;

    if(debug & DB_TRACE)
      fputs("Performing chmod operations\n",stderr);
    for(;nadd;nadd--,ap++)
      if((*ap)->flags & FNF_MODEONLY) {
	  if(dst->pipe) {
	      namesubst(scratch, dst->pipe, NULL, dst, *ap, 'M');
	      if((!scratch[0] || !system(scratch)) &&
		 !((*ap)->flags & FNF_MOVE))
		(*ap)->flags |= FNF_DONE;
	  } else {
	      strcpy(scratch,dst->dir0);
	      s = scratch+strlen(scratch);
	      if(s == scratch || s[-1] != '/')
		*s++ = '/';
	      strcpy(s,(*ap)->dir+strlen((*ap)->root));
	      strcat(scratch,(*ap)->name);
	      if(!chmod(scratch,(*ap)->mode&~S_IFMT) && !((*ap)->flags & FNF_MOVE))
		(*ap)->flags |= FNF_DONE;
	  }
      }
}

void mir_del(struct ftpdir *dst, struct fname **dp, int ndel, char filtdir)
{
    const char *s;
    char *d;
    struct fname **n;
    size_t j;

    if(debug & DB_TRACE)
      fputs("Performing remove operations\n",stderr);
    /* sort for depth-first deletion */
    sort((const struct fname **)dp,ndel,PROCCMP);
    for(;ndel;ndel--,dp++) {
	if((*dp)->flags & FNF_MOVE) /* ignore moves */
	  continue;
	if((*dp)->flags & FNF_APPEND)
	  continue;
	if(dst->pipe) {
	    namesubst(scratch, dst->pipe, NULL, dst, *dp, '-');
	    if(!scratch[0] || !system(scratch))
	      (*dp)->flags |= FNF_DONE;
	} else {
	    strcpy(scratch,(*dp)->dir);
	    strcat(scratch,(*dp)->name);
	    if(debug & DB_OTHER)
	      fprintf(stderr,"Removing %s%s\n",(*dp)->dir,(*dp)->name);
	    /* rm -f */
	    if(remove(scratch) && rmdir(scratch)) {
		/* try to fudge dir permissions to remove file */
		/* but only if dir to be removed, too */
		s = (*dp)->dir;
		if(!filtdir) {
		    /* first non-matching dir should be parent dir */
		    for(n=dp,j=ndel;j;n++,j--)
		      if(s != (*n)->dir && strcmp(s,(*n)->dir))
			break;
		    if(j) {
			for(j=strlen(s)-2;j>=0;j--)
			  if(s[j] == '/')
			    break;
			if(strncmp((*n)->dir,s,j+1) ||
			   strlen((*n)->dir) != j ||
			   strncmp((*n)->name, s+j+1,strlen((*n)->name)) ||
			   (*(s += strlen((*n)->name)) && *s != '/')) {
			    if(!chmod((*n)->dir,0700) && (!remove(scratch) ||
							  !rmdir(scratch)))
			      s = NULL; /* flag that it's deleted */
			}
		    }
		}
		if(s) { /* s will be NULL'd only if item was deleted */
		    if(debug & DB_TRACE)
		      fprintf(stderr,"Can't delete %s\n",scratch);
		    continue;
		}
	    }
	    (*dp)->flags |= FNF_DONE;
	    if(debug & DB_TRACE)
	      fprintf(stderr,"Deleted %s\n",scratch);
	    /* need to add following to pipe, too */
	    if(filtdir) { /* if dirs not explicit, remove empty directories */
		d = strrchr(scratch,'/');
		if(!d[1]) /* trailing / */
		  while(*--d != '/' && d > scratch);
		while(d) {
		    *d = 0;
		    if(rmdir(scratch))
		      break;
		}
	    }
	}
    }
}

void mir_move(struct ftpdir *dst, struct fname **mirarray, int malen,
	      int *nadd, int *ndel)
{
    int i = *ndel, j = *nadd, k = 0, l;
    struct fname **o, **n;
    FILE *lf, *rf;
    struct utimbuf ut;

    /* to prevent sorting everything again, remove finished items */
    for(o=n=mirarray;j;j--,o++) {
	if((*o)->flags & FNF_DONE)
	  continue;
	if((*o)->flags & FNF_MOVE)
	  k++;
	*n++ = *o;
    }
    *nadd = n-mirarray;
    for(o=n=mirarray+malen,j=i;j;j--) {
	if((*--o)->flags & FNF_DONE) {
	    i--;
	    continue;
	}
	*--n = *o;
    }
    *ndel = i;
    j = *nadd;
    if(k) {
	n = mirarray;
	o = mirarray+malen-i;
	sort((const struct fname **)n,j,MVCMP);
	sort((const struct fname **)o,i,MVCMP);
	for(k=i;k && j;k--,j--,o++,n++) {
	    while(k && !((*o)->flags & FNF_MOVE)) {
		k--;
		o++;
	    }
	    if(!k)
	      break;
	    while(j && !((*n)->flags & FNF_MOVE)) {
		j--;
		n++;
	    }
	    if(!j)
	      break;
	    /* perform mv */
	    if(dst->pipe) {
		/* FIXME: *n not anywhere in here */
		namesubst(scratch, dst->pipe, NULL, dst, *o, '>');
		if(!scratch[0] || !system(scratch)) {
		    (*o)->flags |= FNF_DONE;
		    (*n)->flags |= FNF_DONE;
		}
	    } else {
		strcpy(scratch+2048,(*o)->dir);
		strcat(scratch+2048,(*o)->name);
		strcpy(scratch,(*o)->root);
		strcat(scratch,(*n)->dir+strlen((*n)->root));
		strcat(scratch,(*n)->name);
		if(!rename(scratch+2048,scratch)) {
		    (*o)->flags |= FNF_DONE;
		    (*n)->flags |= FNF_DONE;
		} else {
		    /* try copy + rm */
		    if((rf = fopen(scratch+2048,"r")) &&
		       (lf = fopen(scratch,"w")))
		      while((l = nosig_fread(scratch+4096,1,4096,rf)) &&
			    nosig_fwrite(scratch+4096,l,1,lf) == 1);
		    if(rf && lf && !ferror(lf)) {
			if(!fclose(lf)) {
			    struct stat st;

			    (*o)->flags |= FNF_DONE;
			    (*n)->flags |= FNF_DONE;
			    chmod(scratch,(*n)->mode&~S_IFMT);
			    if(!stat(scratch+2048,&st)) {
				ut.actime = st.st_atime;
				ut.modtime = st.st_mtime;
				utime(scratch,&ut);
			    } /* else it'll just have to stay bad */
			    remove(scratch+2048);
			} else
			  remove(scratch);
		    } else {
			if(rf && lf) {
			    fclose(lf);
			    remove(scratch);
			}
		    }
		    if(rf)
		      fclose(rf);
		}
	    }
	}
    }
}

void mir_transfer(struct fname **ap, int nadd, struct ftpdir *src,
		  const struct ftpdir *dst, char mirstmp, char mirmodes,
		  char mirappend)
{
    int j;
    char *s;
    struct utimbuf ut;
    struct tm tm;
    FILE *lf = NULL; /* lf = NULL to keep GCC happy */

    if(debug & DB_TRACE)
      fputs("Performing add operations\n",stderr);
    /* resort for reverse depth order */
    sort((const struct fname **)ap,nadd,PROCCMP);
    for(;nadd;nadd--,ap++) {
	/* ignore moves & chmods */
	if((*ap)->flags & (FNF_MOVE|FNF_MODEONLY))
	  continue;
	/* open local file for regular files */
	if(dst->pipe) {
	    /* pipe: local file is tmp file, but "local file name"
	     * is the command (with %-substitutions) to run */
	    /* pipe is only run for regular files */
	    if(!S_ISREG((*ap)->mode) || mirstmp) {
		(*ap)->flags |= FNF_DONE;
		continue;
	    }
	    namesubst(scratch, dst->pipe, &src->site, dst, *ap, '+');
	    if(!scratch[0]) {
		(*ap)->flags |= FNF_DONE;
		continue;
	    }
	    if(!mirstmp) {
		lf = popen(scratch, "w");
		if(!lf) {
		    lockof();
		    fprintf(of, "*** Can't open pipe %s ***\n", scratch);
		    unlockof();
		    continue;
		}
	    }
	} else {
	    /* non-piping */
	    /* construct local name already for non-file, too */
	    strcpy(scratch,dst->dir0);
	    s = scratch+strlen(scratch);
	    if(s == scratch || s[-1] != '/')
	      *s++ = '/';
	    strcpy(s,(*ap)->dir+strlen((*ap)->root));
	    /* should only need mkpath if(filtdir), but just in case.. */
	    mkpath(scratch); /* make dir to put file into */
	    strcat(s,(*ap)->name);
	    /* only open the file for regulars */
	    if(!mirstmp && S_ISREG((*ap)->mode) &&
	       !(lf = fopen(scratch, (*ap)->flags & FNF_APPEND ? "a" : "w"))) {
		perror(scratch);
		continue; /* needn't try retrieving any more */
	    }
	}
	/* construct remote name in s */
	s = scratch+strlen(scratch)+1; /* hope there's enough room... */
	strcpy(s,(*ap)->dir);
	strcat(s,(*ap)->name);
	/* construct time stamp already so it's ready for non-files */
	/* this had to wait until here so that the remote name was */
	/* already constructed */
	if(!S_ISLNK((*ap)->mode)) {
	    time(&ut.actime);
	    if(ftp_filetm(&src->site, s, &tm))
	      /* tm has UTC stamp of file */
	      ut.modtime = mktime(&tm);
	    else {
		tm.tm_sec = 0;
		tm.tm_min = (*ap)->min;
		if(tm.tm_min < 0)
		  tm.tm_min = 0;
		tm.tm_hour = (*ap)->hr;
		if(tm.tm_hour < 0)
		  tm.tm_hour = 12; /* good as any time, I guess */
		tm.tm_mday = (*ap)->day;
		tm.tm_mon = (*ap)->month-1;
		tm.tm_year = (*ap)->year-1900;
		tm.tm_isdst = 0;
		ut.modtime = mktime(&tm) - timezone;
	    }
	    if(ut.modtime == -1)
	      ut.modtime = ut.actime;
	    /* if just stamping, stamp & continue */
	    /* FIXME:  this should work with pipes, too */
	    if(mirstmp && !dst->pipe && access(scratch,F_OK)) {
		if(mirmodes)
		  chmod(scratch,(*ap)->mode&~S_IFMT);
		utime(scratch,&ut);
	    }
	}
	if(mirstmp)
	  continue;
	/* if not a regular file, create node and continue with next */
	if(!S_ISREG((*ap)->mode)) {
	    if(dst->pipe) /* FIXME:  yet another failure for pipes */
	      continue;
	    if(debug & DB_TRACE)
	      fprintf(stderr,"Making node %s\n",scratch);
	    if(!mirmodes)
	      (*ap)->mode = ((*ap)->mode & S_IFMT) | 0777;
	    /* I don't want to mess with sockets yet; they fail */
	    if(S_ISDIR((*ap)->mode)?(mkdir(scratch,(*ap)->mode) &&
				     (!mkpath(scratch) ||
				      (mirmodes &&
				       chmod(scratch,(*ap)->mode)))):
	       S_ISLNK((*ap)->mode)?symlink((*ap)->lnk,scratch):
	       S_ISSOCK((*ap)->mode)?-1:
	       mknod(scratch,(*ap)->mode,
		     makedev((*ap)->size>>16,(*ap)->size&0xffff)))
	      perror(scratch);
	    else
	      (*ap)->flags |= FNF_DONE;
	    if(!S_ISLNK((*ap)->mode))
	      utime(scratch,&ut);
	    continue;
	}
	/* retrieve regular file */
	/* actually retrieve the file w/ to's & retries */
	j = getfile(lf, &src->site, s);
	if(j || fflush(lf)) {
	    if(!j || j == ERR_OS)
	      perror(scratch);
	    fclose(lf);
	    if(!dst->pipe)
	      remove(scratch);
	    if(j == ERR_MAXRETR) /* don't try every file to max retry */
	      break;
	    continue;
	}
	/* now process the results */
	if(dst->pipe) {
	    if(!pclose(lf))
	      (*ap)->flags |= FNF_DONE;
	} else {
	    /* file: close file & set mode+date */
	    fclose(lf); /* fflush above checked err */
	    /* don't really care if chmod/utime fail */
	    (*ap)->flags |= FNF_DONE;
	    if(mirmodes)
	      chmod(scratch,(*ap)->mode&~S_IFMT);
	    utime(scratch,&ut);
	}
    }
}
