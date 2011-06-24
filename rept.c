/*
 * Compare and report functions for lurkftp
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

static char *addtot(char *buf, const char *type, unsigned long bytes,
		    unsigned long files, unsigned long dirs,
		    unsigned long specials)
{
    int got1 = 0;
    const char *sz;

    if(!bytes && !files && !dirs && !specials)
      return buf;
    if(dirs) {
	buf += sprintf(buf,"%lu directories", dirs);
	got1 = 1;
    }
    if(specials) {
	buf += sprintf(buf,"%s%lu special nodes", got1?", ":"",specials);
	got1 = 1;
    }
    if(files) {
	if(bytes < 10000)
	  sz = "bytes";
	else {
	    bytes /= 1024;
	    if(bytes < 10000)
	      sz = "KB";
	    else {
		bytes /= 1024;
		if(bytes < 10000)
		  sz = "MB";
		else
		  sz = "GB";
	    }
	}
	buf += sprintf(buf,"%s%lu %s in %lu files",got1?", ":"",bytes,sz,files);
    }
    buf += sprintf(buf," %s\n",type);
    return buf;
}

/* perform %-substitutions for lsfile names & headers/footers */
/* FIXME: add string for err msg */
void headsubst(register char *buf, register const char *pat,
	       const struct ftpdir *site, const struct ftpdir *other,
	       const struct totals *tot)
{
    int i;
    char *s;
    int wid, maxc;
    char rl;
    char *bufs = buf;

    if(debug & DB_OTHER)
      fprintf(stderr,"head: '%s' -> ",pat);
    for(;*pat;pat++) {
	if(*pat != '%')
	  *buf++ = *pat;
	else {
	    wid = maxc = -1;
	    if(*++pat == '-' && isdigit(pat[1])) {
		rl = 1;
		pat++;
	    } else
	      rl = 0;
	    while(isdigit(*pat)) {
		if(wid == -1)
		  wid = *pat-'0';
		else
		  wid = (wid*10)+(*pat-'0');
		pat++;
	    }
	    if(*pat == '.' && isdigit(pat[1])) {
		while(isdigit(*++pat)) {
		    if(maxc == -1)
		      maxc = *pat-'0';
		    else
		      maxc = (maxc*10)+(*pat-'0');
		}
	    }
	    switch(*pat) {
	      case 's':
		if(!site->site.host)
		  break;
		if(maxc >= 0)
		  strncpy(buf,site->site.host,maxc);
		else
		  strcpy(buf,site->site.host);
		maxc = strlen(buf);
		break;
	      case 'd':
		if(!site->ndirs) {
		    maxc = 0;
		    break;
		}
		strcpy(buf,site->dirs[0]);
		s = buf+strlen(buf);
		for(i = 1; i< site->ndirs; i++) {
		    *s++ = '_';
		    strcpy(s, site->dirs[i]);
		    s += strlen(s);
		}
		s = buf;
		do {
		    if(*s == ' ')
		      *s = '_';
		    else if(*s == '/')
		      *s = '.';
		} while(*++s);
		maxc = s - buf;
		break;
	      case 'p':
		if(!site->ndirs) {
		    maxc = 0;
		    break;
		}
		strcpy(buf,site->dirs[0]);
		s = buf+strlen(buf);
		for(i = 1; i< site->ndirs; i++) {
		    *s++ = ' ';
		    strcpy(s, site->dirs[i]);
		    s += strlen(s);
		}
		maxc = s - buf;
		break;
	      case 'S':
		if(!other->site.host)
		  break;
		if(maxc >= 0)
		  strncpy(buf,other->site.host,maxc);
		else
		  strcpy(buf,other->site.host);
		maxc = strlen(buf);
		break;
	      case 'D':
		if(!other->ndirs) {
		    maxc = 0;
		    break;
		}
		strcpy(buf,other->dirs[0]);
		s = buf+strlen(buf);
		for(i = 1; i< other->ndirs; i++) {
		    *s++ = '_';
		    strcpy(s, other->dirs[i]);
		    s += strlen(s);
		}
		s = buf;
		do {
		    if(*s == ' ')
		      *s = '_';
		    else if(*s == '/')
		      *s = '.';
		} while(*++s);
		maxc = s - buf;
		break;
	      case 'P':
		if(!other->ndirs) {
		    maxc = 0;
		    break;
		}
		strcpy(buf,other->dirs[0]);
		s = buf+strlen(buf);
		for(i = 1; i< other->ndirs; i++) {
		    *s++ = ' ';
		    strcpy(s, other->dirs[i]);
		    s += strlen(s);
		}
		maxc = s - buf;
		break;
	      case 't':
		/* totals */
		if(tot) {
		    buf = addtot(buf, "added", tot->addb, tot->addf, tot->addd,
				 tot->adds);
		    buf = addtot(buf, "removed", tot->subb, tot->subf, tot->subd,
				 tot->subs);
		}
		maxc = wid = -1;
		break;
	      default:
		*buf++ = *pat;
		maxc = wid = -1;
		break;
	    }
	    if(maxc < 0)
	      maxc = 0;
	    if(wid > maxc) {
		if(rl)
		  while(wid > maxc)
		    buf[maxc++] = ' ';
		else {
		    for(s = buf + maxc; s > buf; s--)
		      s[wid-maxc-1] = s[-1];
		    while(wid > maxc) {
			*buf++ = ' ';
			wid--;
		    }
		}
	    }
	    buf += maxc;
	}
    }
    *buf = 0;
    if(debug & DB_OTHER)
      fprintf(stderr,"'%s'\n",bufs);
}

/* perform %-substitutions for names & pipes */
void namesubst(register char *buf, const char *pat, const struct ftpsite *src,
	       const struct ftpdir *dst, const struct fname *item,
	       char type)
{
    register char *t;
    struct tm tm;
    int wid, maxc;
    unsigned char rl;
    unsigned char bnest = 0, modval;
    mode_t xmod = 0;
    char *bufs = buf;
    size_t sz;

    if(debug & DB_OTHER)
      fprintf(stderr,"name: '%s' -> ",pat);
    for(;*pat;pat++) {
	if(*pat == '%') {
	    wid = maxc = -1;
	    if(*++pat == '-' && isdigit(pat[1])) {
		rl = 1;
		pat++;
	    } else
	      rl = 0;
	    while(isdigit(*pat)) {
		if(wid == -1)
		  wid = *pat-'0';
		else
		  wid = (wid*10)+(*pat-'0');
		pat++;
	    }
	    if(*pat == '.' && isdigit(pat[1])) {
		while(isdigit(*++pat)) {
		    if(maxc == -1)
		      maxc = *pat-'0';
		    else
		      maxc = (maxc*10)+(*pat-'0');
		}
	    }
	    switch(*pat) {
	      case 'T':
		*buf = type;
		maxc = 1;
		break;
	      case 's':
		if(src) {
		    strcpy(buf,src->host);
		    maxc = strlen(buf);
		} else
		  maxc = 0;
		break;
	      case 'f':
		strcpy(buf,item->name);
		maxc = strlen(buf);
		break;
	      case 'L':
		if(S_ISLNK(item->mode)) {
		    strcpy(buf,item->lnk);
		    maxc = strlen(buf);
		} else
		  maxc = 0;
		break;
	      case '[':
		modval = 0;
		maxc = wid = 0;
		while(*++pat && *pat != '?' && *pat != ':') {
		    xmod &= ~S_IFMT;
		    if(!modval)
		      switch(*pat) {
			case 'B':
			  modval = 0;
			  switch(pat[1]) {
			    case '=':
			      pat++;
			      modval = 1;
			      break;
			    case '>':
			      pat++;
			      break;
			    case '<':
			      pat++;
			      modval = 2;
			      break;
			  }
			  sz = 0;
			  while(isdigit(pat[1]))
			    sz = sz*10 + *++pat - '0';
			  switch(pat[1]) {
			    case 'K':
			      sz *= 1024;
			      pat++;
			      break;
			    case 'B':
			      sz *= 512;
			      pat++;
			      break;
			    case 'M':
			      sz *= 1024*1024;
			      pat++;
			      break;
			  }
			  if(!S_ISREG(item->mode)) {
			      modval = 0;
			      break;
			  }
			  switch(modval) {
			    case 0:
			      modval = item->size > sz;
			      break;
			    case 1:
			      modval = item->size == sz;
			      break;
			    case 2:
			      modval = item->size < sz;
			      break;
			  }
			  break;
			case 'l':
			  if(S_ISLNK(item->mode))
			    modval = 1;
			  break;
			case 'f':
			  if(S_ISREG(item->mode))
			    modval = 1;
			  break;
			case 'd':
			  if(S_ISDIR(item->mode))
			    modval = 1;
			  break;
			case 's':
			  if(S_ISSOCK(item->mode))
			    modval = 1;
			  break;
			case 'b':
			  if(S_ISBLK(item->mode))
			    modval = 1;
			  break;
			case 'c':
			  if(S_ISCHR(item->mode))
			    modval = 1;
			  break;
			case 'p':
			  if(S_ISFIFO(item->mode))
			    modval = 1;
			  break;
			case 't':
			  if(item->mode & S_ISVTX)
			    modval = 1;
			  break;
			case 'u':
			  xmod |= S_IFMT|S_IRWXU|S_ISUID;
			  break;
			case 'g':
			  xmod |= S_IFMT|S_IRWXG|S_ISGID;
			  break;
			case 'o':
			  xmod |= S_IFMT|S_IRWXO;
			  break;
			case 'r':
			  if(!xmod && (item->mode & (S_IRUSR|S_IRGRP|S_IROTH)))
			    modval = 1;
			  else if(xmod && (item->mode & xmod & (S_IRUSR|S_IRGRP|S_IROTH)))
			    modval = 1;
			  xmod |= S_IFMT;
			  break;
			case 'w':
			  if(!xmod && (item->mode & (S_IWUSR|S_IWGRP|S_IWOTH)))
			    modval = 1;
			  else if(xmod && (item->mode & xmod & (S_IWUSR|S_IWGRP|S_IWOTH)))
			    modval = 1;
			  xmod |= S_IFMT;
			  break;
			case 'x':
			  if(!xmod && (item->mode & (S_IXUSR|S_IXGRP|S_IXOTH)))
			    modval = 1;
			  else if(xmod && (item->mode & xmod & (S_IXUSR|S_IXGRP|S_IXOTH)))
			    modval = 1;
			  xmod |= S_IFMT;
			  break;
			case 'S':
			  if(!xmod && (item->mode & (S_ISUID|S_ISGID)))
			    modval = 1;
			  else if(xmod && (item->mode & xmod & (S_ISUID|S_ISGID)))
			    modval = 1;
			  xmod |= S_IFMT;
			  break;
			case 'T':
			  if(pat[1] && *++pat == type)
			    modval = 1;
			  break;
		      }
		    if(!(xmod & S_IFMT))
		      xmod = 0;
		}
		if(*pat) {
		    if((modval && *pat == '?') || (!modval && *pat == ':'))
		      bnest++;
		    else {
			modval = 0;
			while(*++pat) {
			    if(*pat == '%') {
				if(*++pat == '[')
				  modval++;
				else if(!*pat)
				  break;
			    } else if(*pat == ']') {
				if(!modval--)
				  break;
			    } else if(!modval && *pat == ':') {
				bnest++;
				break;
			    }
			}
		    }
		}
		break;
	      case 'r':
		strcpy(buf,item->dir);
		maxc = strlen(buf);
		break;
	      case 'l':
		if(dst && dst->dir0)
		  strcpy(buf,dst->dir0);
		else
		  strcpy(buf,item->root); /* better than nothing */
		if(!*buf)
		  *buf++ = '/';
		buf += strlen(buf);
		if(buf[-1] != '/')
		  *buf++ = '/';
		if(item->dir[strlen(item->root)] == '/')
		  buf--;
		strcpy(buf,item->dir+strlen(item->root));
		maxc = strlen(buf);
		break;
	      case 'b':
		maxc = sprintf(buf,"%lu",item->size);
		break;
	      case 'm':
		maxc = sprintf(buf,"0%o",item->mode);
		break;
	      case 'D':
		if(S_ISCHR(item->mode) || S_ISBLK(item->mode))
		  maxc = sprintf(buf,"%lu",item->size>>16);
		else
		  maxc = 0;
		break;
	      case 'M':
		if(S_ISCHR(item->mode) || S_ISBLK(item->mode))
		  maxc = sprintf(buf,"%lu",item->size&0xffff);
		else
		  maxc = 0;
		break;
	      case 'd':
		maxc = sprintf(buf,"%4hu%02hu%02hu",item->year,item->month,item->day);
		break;
	      case 't':
		maxc = sprintf(buf,"%02hu:%02hu",item->hr,item->min);
		break;
	      case '{':
		t = strchr(pat, '}');
		tm.tm_sec = 0;
		tm.tm_min = item->min;
		if(tm.tm_min < 0)
		  tm.tm_min = 0;
		tm.tm_hour = item->hr;
		if(tm.tm_hour < 0)
		  tm.tm_hour = 12; /* good as any time */
		tm.tm_mday = item->day;
		tm.tm_mon = item->month-1;
		tm.tm_year = item->year-1900;
		tm.tm_isdst = 0;
		/* 64 is enough for anyone, I'm sure */
		memcpy(buf+64,pat,t-pat);
		buf[64+t-pat] = 0;
		maxc = strftime(buf,64,buf,&tm);
		if(t)
		  pat = t;
		break;
	      default:
		*buf++ = *pat;
		maxc = wid = 0;
		break;
	    }
	    if(maxc < 0)
	      maxc = 0;
	    if(wid > maxc) {
		if(rl)
		  while(wid > maxc)
		    buf[maxc++] = ' ';
		else {
		    for(t = buf + maxc; t > buf; t--)
		      t[wid-maxc-1] = t[-1];
		    while(wid > maxc) {
			*buf++ = ' ';
			wid--;
		    }
		}
	    }
	    buf += maxc;
	} else if(bnest && *pat == ']')
	      bnest--;
	else if(bnest && *pat == ':') {
	    modval = 0;
	    while(*++pat) {
		if(*pat == '%')
		  if(*++pat == '[')
		    modval++;
		else if(*pat == ']')
		  if(!modval--)
		    break;
		else if(!modval && *pat == ':')
		  break;
	    }
	    if(*pat == ']')
	      bnest--;
	} else
	  *buf++ = *pat;
    }
    *buf = 0;
    if(debug & DB_OTHER)
      fprintf(stderr,"'%s'\n",bufs);
}

static char prname(char didrept, const char *fmt, char prefix,
		   const struct fname *n, const char *header,
		   const struct ftpdir *site, const struct ftpdir *other)
{
    char *s;

    namesubst(scratch, fmt, NULL, NULL, n, prefix);
    if(!scratch[0])
      return didrept;
    if(!didrept) {
	didrept = 1;
	lockof();
	s = scratch+strlen(scratch)+1;
	headsubst(s,header, site, other, NULL);
	if(*s) {
	    fputs(s, of);
	    if(s[strlen(s)-1] != '\n')
	      fputc('\n', of);
	}
	didrept = 1;
    }
    fputs(scratch, of);
    if(scratch[strlen(scratch)-1] != '\n')
      fputc('\n', of);
    return didrept;
}

void pr_rept(struct fname **mirarray, int malen, int nadd, int ndel,
	     const char *header, const char *tail, const struct ftpdir *site,
	     const struct ftpdir *other, const char *fmt, unsigned _sort,
	     char resort)
{
    const struct fname **o, **n;
    int i, j, k;
    char didrept = 0;
    struct totals tot = {0};

    /* print report */
    i = ndel;
    j = nadd;
    o = (const struct fname **)mirarray+malen-ndel;
    n = (const struct fname **)mirarray;
    if(resort) {
	/* resort by full name */
	sort(o, i, _sort);
	sort(n, j, _sort);
    } else
      o += ndel-1; /* reverse order sort on deletions */
    sorder = _sort;
    while(i || j) {
	if(!i)
	  k = 1;
	else if(!j)
	  k = -1;
	else
	  k = gencmp(o,n);
	if(k <= 0) {
	    if(S_ISREG((*o)->mode)) {
		tot.subf++;
		tot.subb += (*o)->size;
	    } else if(S_ISDIR((*o)->mode)) /* && !filtdir */
		  tot.subd++;
	    else
	      tot.subs++;
	    didrept = prname(didrept, fmt,((*o)->flags&FNF_MOVE)?'<':'-',*o,
			     header, site, other);
	    i--;
	    if(resort)
	      o++;
	    else
	      o--;
	} else {
	    if(S_ISREG((*n)->mode)) {
		tot.addf++;
		tot.addb += (*n)->size;
	    } else if(S_ISDIR((*n)->mode))
		  tot.addd++;
	    else
	      tot.adds++;
	    if((*n)->flags & FNF_MODEONLY) {
		didrept = prname(didrept, fmt,'M',*n, header, site, other);
		if((*n)->flags & FNF_MOVE)
		  didrept = prname(didrept, fmt,'>',*n, header, site, other);
	    } else
	      didrept = prname(didrept, fmt,((*n)->flags&FNF_MOVE)?'>':'+',*n,
			       header, site, other);
	    n++;
	    j--;
	}
    }
    if(didrept) {
	headsubst(scratch, tail, site, other, &tot);
	if(*scratch) {
	    fputs(scratch, of);
	    if(scratch[strlen(scratch)-1] != '\n')
	      fputc('\n', of);
	}
	unlockof();
    }
}

/* report mirroring errors */
void mir_errrpt(struct fname **ap, struct fname **dp, int nadd,
		int ndel, const char *header, const char *tail,
		const struct ftpdir *site, const struct ftpdir *other,
		const char *fmt)
{
    int anyout = 0;

    /* report errors in deletions */
    for(;ndel;ndel--,dp++) {
	if((*dp)->flags & FNF_DONE)
	  continue;
	anyout = prname(anyout,fmt,(*dp)->flags & FNF_MOVE?'(':'#',*dp,
			header,site,other);
    }
    /* report errors in downloads */
    for(;nadd;ap++,nadd--) {
	if((*ap)->flags & FNF_DONE)
	  continue;
	anyout = prname(anyout,fmt,(*ap)->flags & FNF_MOVE?')':
			(*ap)->flags & FNF_MODEONLY?'$':'*',*ap,header,site,
			other);
    }
    if(anyout) {
	headsubst(scratch, tail, site, other, NULL);
	if(*scratch) {
	    fputs(scratch, of);
	    if(scratch[strlen(scratch)-1] != '\n')
	      fputc('\n', of);
	}
	unlockof();
    }
}
