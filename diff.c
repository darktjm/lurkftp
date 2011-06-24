/*
 * Compare/diff functions for lurkftp
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

/* sort order; qsort() doesn't take parms, so this is static */
unsigned sorder;

unsigned strip_from_sorder(unsigned sorder, unsigned char what)
{
    unsigned n_sorder = sorder, xmask = 0xf, imask=0;

    while(n_sorder)
    {
	if((n_sorder & 0xf) == what)
	  break;
	n_sorder >>=4;
	imask |= xmask;
	xmask <<=4;
    }
    if(!n_sorder)
      return sorder;
    return (sorder&imask) | ((sorder>>4)&~imask);
}

/* for following compare function: compare 2 directory names */
static int dircmp(const struct fname *a, const struct fname *b)
{
    int i,j;

    if(a->root != b->root && a->root && b->root) { /* probably mirroring */
	i = strlen(a->root);
	j = strlen(b->root);
	if(!i || a->root[i-1] != '/')
	  i++;
	if(!j || b->root[j-1] != '/')
	  j++;
	return strcmp(a->dir+i,b->dir+j);
    } else
      return strcmp(a->dir, b->dir);
}

/*
 * Generic sort routine
 * sort order is in string:
 * 0 = f = filetype
 * 1 = m = mode
 * 2 = d = date (YYYY.MM.DD) (if file)
 * 3 = p = dir (path)
 * 4 = n = name
 * 5 = l = link name (if present)
 * 6 = s = size (if file)
 * 7 = t = time (HH:MM) (if file)
 * |8 = caps = reverse sort order.
 */
unsigned parsesort(const char *str)
{
    unsigned res = 0;
    while(*str) {
	res <<= 4;
	switch(*str++) {
	  case 'f':
	    res |= 0;
	    break;
	  case 'F':
	    res |= 8;
	    break;
	  case 'm':
	    res |= 1;
	    break;
	  case 'M':
	    res |= 9;
	    break;
	  case 'd':
	    res |= 2;
	    break;
	  case 'D':
	    res |= 10;
	    break;
	  case 'p':
	    res |= 3;
	    break;
	  case 'P':
	    res |= 11;
	    break;
	  case 'n':
	    res |= 4;
	    break;
	  case 'N':
	    res |= 12;
	    break;
	  case 'l':
	    res |= 5;
	    break;
	  case 'L':
	    res |= 13;
	    break;
	  case 's':
	    res |= 6;
	    break;
	  case 'S':
	    res |= 14;
	    break;
	  case 't':
	    res |= 7;
	    break;
	  case 'T':
	    res |= 15;
	    break;
	}
    }
    return res;
}

int gencmp(const void *__a, const void *__b)
{
    const struct fname * const * const _a = __a, * const * const _b = __b;
    const struct fname * const au = *_a, * const bu = *_b;
    const struct fname *a, *b;
    int md;
    int i;
    register unsigned s;

    md = (au->mode&S_IFMT)-(bu->mode&S_IFMT);
    for(s = sorder; s; s >>= 4) {
	if(s & 8) {
	    a = bu;
	    b = au;
	} else {
	    a = au;
	    b = bu;
	}
	switch(s & 7) {
	  case 0: /* f = filetype */
	    if(md)
	      return s&8?-md:md;
	    break;
	  case 1: /* m = mode */
	    i = (a->mode&~S_IFMT)-(b->mode&~S_IFMT);
	    if(i)
	      return i;
	    break;
	  case 2: /* d = date */
	    if(!md && S_ISREG(a->mode)) {
		if(a->year != b->year)
		  return a->year - b->year;
		if(a->month != b->month)
		  return a->month - b->month;
		if(a->day != b->day)
		  return a->day - b->day;
	    }
	    break;
	  case 3: /* p = path */
	    if((i = dircmp(a,b)))
	      return i;
	    break;
	  case 4: /* n = name */
	    i = strcmp(a->name,b->name);
	    if(i)
	      return i;
	    break;
	  case 5: /* l = link */
	    if(!md && S_ISLNK(a->mode)) {
		i = strcmp(a->lnk, b->lnk);
		if(i)
		  return i;
	    }
	    break;
	  case 6: /* s = size */
	    if(!md && S_ISREG(a->mode) && (i = (int)a->size - (int)b->size))
	      return i;
	    break;
	  case 7: /* t = time */
	    /* Check hour & minute, only if all else is the same */
	    /* otherwise diffing may be screwed up because remote list */
	    /* doesn't have hr+min, but local stuff does */
	    if(!md && S_ISREG(a->mode)) {
		if(a->hr != b->hr && a->hr >= 0 && b->hr >= 0)
		  return a->hr - b->hr;
		if(a->min != b->min && a->min >= 0 && b->min >= 0)
		  return a->min - b->min;
	    }
	    break;
	}
    }
    return 0;
}

/* convenience macro for move detection; see description below */
#define chk1mov(a,b,i,j) do { \
    while(j && !(k=gencmp(&a[-1],b)) && !dircmp(a[-1],*b)) { \
	b++; \
	j--; \
	if(chkmodes && a[-1]->mode != b[-1]->mode) { \
	    n[-1]->flags |= FNF_MODEONLY|FNF_PROCESS; \
	    *ap++ = n[-1]; \
	} \
	if(!i) { \
	    k = 0; /* flag: bad */ \
	    break; \
	} \
	a++; \
	i--; \
    } \
    if(j && i && !k) { \
	while(j && i && !(k=gencmp(a,b)) && !dircmp(*a,*b)) { \
	    if(chkmodes && (*a)->mode != (*b)->mode) { \
		(*b)->flags |= FNF_MODEONLY|FNF_PROCESS; \
		*ap++ = *b; \
	    } \
	    a++; \
	    b++; \
	    i--; \
	    j--; \
	} \
	/* flag bad if any more near-matches */ \
	k = (!i || gencmp(&a[-1],a)) && (!j || gencmp(&b[-1],b)); \
    } else if(j && !gencmp(&b[-1],b)) \
	  k = 0; /* flag: bad */ \
    if(k && !gencmp(&a[-1],&b[-1])) { /* good */ \
	a[-1]->flags |= FNF_MOVE|FNF_PROCESS; \
	b[-1]->flags |= FNF_MOVE|FNF_PROCESS; \
	*ap++ = n[-1]; \
	*--dp = o[-1]; \
	continue; \
    } \
} while(0)

struct fname **diff_dir(struct ftpdir *src, struct ftpdir *dst,
			int *arraylen, int *nadd, int *ndel,
			char detmove, char chkmodes, char appendmode)
{
    struct fname **mirarray, **ap, **dp, **o, **n;
    int i, j, k;

    *arraylen = src->dirm.count+dst->dirm.count;
    /* create difference array big enough to hold all old & new items */
    ap = mirarray = malloc(*arraylen*sizeof(*ap));
    if(!ap) {
	perror("diff tmp storage");
	return NULL;
    }
    dp = ap+*arraylen; /* deletions stored at top of list */
    /* since remt and local sorted in same order, diff by running */
    /* running up both lists; which ever pointer is lower in comparison */
    /* gets incremented & reported. */
    if(debug & DB_TRACE)
      fputs("Finding differences in listings\n",stderr);
    sorder = detmove?MVCHK:REPTCMP;
    for(o=dst->dirm.array,n=src->dirm.array,i=dst->dirm.count,j=src->dirm.count;j && i;) {
	k = gencmp(n,o);
	if(debug & DB_OTHER)
	  fprintf(stderr,"diff: %02d:%02d %02d/%02d/%04d (%ld/%o) %s%s%s%s - "
		  " %02d:%02d %02d/%02d/%04d (%ld/%o) %s%s%s%s: %d\n",
		  (*o)->hr,(*o)->min,(*o)->month,(*o)->day,(*o)->year,
		  (*o)->size,(unsigned)(*o)->mode,(*o)->dir,(*o)->name,
		  S_ISLNK((*o)->mode)?" -> ":"",
		  S_ISLNK((*o)->mode)?(*o)->lnk:"",
		  (*n)->hr,(*n)->min,(*n)->month,(*n)->day,(*n)->year,
		  (*n)->size,(unsigned)(*n)->mode,(*n)->dir,(*n)->name,
		  S_ISLNK((*n)->mode)?" -> ":"",
		  S_ISLNK((*n)->mode)?(*n)->lnk:"",
		  k);
	if(!k) {
	    n++;
	    o++;
	    i--;
	    j--;
	    /* extra check for mere mode change */
	    if(chkmodes && o[-1]->mode != n[-1]->mode)
	      n[-1]->flags |= FNF_MODEONLY|FNF_PROCESS;
	    /* extra check for mere move */
	    if(detmove && (k=dircmp(n[-1],o[-1]))) {
		/* now make sure only one file moved */
		/* procedure: check if additional items match.  */
		/* if so, skip ones that are also in the same dir */
		/* finally, if a move condition still exists, */
		/* and no additional items match, flag as moved */
		/* this is in a macro because we need to do the same thing */
		/* with either (o,n) or (n,o) depending on which is higher */
		/* from sort */
		/* procedure is duplicated, so it's written as a macro above */
		if(k < 0)
		  chk1mov(o,n,i,j);
		else
		  chk1mov(n,o,j,i);
	    }
	    /* if not already added by move logic, add for mode change */
	    if(n[-1]->flags&FNF_MODEONLY)
	      *ap++ = n[-1];
	    continue;
	}
	if(k<0) {
	    *ap++ = *n; /* mark added */
	    (*n)->flags |= FNF_PROCESS;
	    n++;
	    j--;
	} else {
	    if(appendmode && ap > mirarray &&
	       (*o)->size < ap[-1]->size &&
	       !dircmp(ap[-1], *o))
	    {
		unsigned x = sorder;
		
		sorder = strip_from_sorder(sorder, 6);
		sorder = strip_from_sorder(sorder, 2);
		if(!gencmp(&ap[-1], o)) {
		    (*o)->flags |= FNF_APPEND;
		    ap[-1]->flags |= FNF_APPEND;
		}
		sorder =x;
	    }
	    *--dp = *o; /* mark removed */
	    (*o)->flags |= FNF_PROCESS;
	    o++;
	    i--;
	}
    }
    /* add any remaining new items */
    while(j) {
	*ap++ = *n; /* mark added */
	(*n)->flags |= FNF_PROCESS;
	n++;
	j--;
    }
    /* remove any remaining old items */
    while(i) {
	if(appendmode && ap > mirarray &&
	   (*o)->size < ap[-1]->size &&
	   !dircmp(ap[-1], *o))
	{
	    unsigned x = sorder;
	    
	    sorder = strip_from_sorder(sorder, 6);
	    sorder = strip_from_sorder(sorder, 2);
	    if(!gencmp(&ap[-1], o)) {
		(*o)->flags |= FNF_APPEND;
		ap[-1]->flags |= FNF_APPEND;
	    }
	    sorder =x;
	}
	*--dp = *o; /* mark removed */
	(*o)->flags |= FNF_PROCESS;
	o++;
	i--;
    }
    *nadd = ap - mirarray;
    *ndel = (mirarray+*arraylen)-dp;
    return mirarray;
}
