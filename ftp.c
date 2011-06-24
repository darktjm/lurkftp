#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h> /* Linux-only */
#include <stddef.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <time.h>
#include "ftp.h"

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

/* default timeouts */
struct to defto = {
    10, /* retrycnt */
    20, /* retrytime */
    20, /* connect */
    5, /* quit */
    10, /* xfer */
    10 /* cmd */
};

#if 0
/* old signal handling code */
/* I don't think this is needed any more, but just in case, I don't */
/* want to forget what I wrote the first time around... */
static sigjmp_buf alarmjmp;

static void doalarm(int ign)
{
    siglongjmp(alarmjmp,1);
}

static int savalid = 0;
static struct sigaction oa[2];

#ifndef SA_RESETHAND
#ifdef SA_ONESHOT
#define SA_RESETHAND SA_ONESHOT
#else
#define SA_RESETHAND 0
#endif
#endif

#define setsig(sig,n) do {\
   sigaction(sig,NULL,&sa); \
   oa[n] = sa; \
   sa.sa_handler = doalarm; \
   sa.sa_flags &= ~SA_RESTART; \
   sa.sa_flags |= SA_RESETHAND; \
   sigaction(sig,&sa,NULL); \
} while(0)

static void setalarm(void)
{
    struct sigaction sa;

    if(savalid)
      return;
    setsig(SIGALRM,0);
    setsig(SIGINT,1);
    savalid = 1;
}

#define clrsig(sig,n) do {\
  if(savalid) \
     sigaction(sig,oa+n,NULL); \
  else \
     signal(sig,SIG_DFL); \
} while(0)

static void clralarm(void)
{
    alarm(0);
    clrsig(SIGALRM,0);
    clrsig(SIGINT,1);
    savalid = 0;
}
#endif

static int timedout = 0, canto = 0;
static void (* oalarm)(int);

static void doalarm(int ign)
{
    if(canto)
      timedout = 1;
    if(oalarm != SIG_IGN && oalarm != SIG_DFL)
      oalarm(ign);
}

static void setalarm(int aval)
{
    struct sigaction sa;

    alarm(0);
    sigaction(SIGALRM,NULL,&sa);
    if(sa.sa_handler != doalarm)
      oalarm = sa.sa_handler;
    sa.sa_handler = doalarm;
    sa.sa_flags &= ~SA_RESTART;
    sa.sa_flags |= SA_RESETHAND;
    sigaction(SIGALRM,&sa,NULL);
    canto = 1;
    timedout = 0;
    alarm(aval);
}

static int clralarm(void)
{
    alarm(0);
    canto = 0;
    if(timedout) {
	timedout = 0;
	return ERR_TO;
    }
    return ERR_OK;
}
    
/* simple telnet stuff */
static int open_tn(const char *host, unsigned short port, struct tvt *tvt)
{
    struct hostent *he;
    char **p;
    unsigned long opt;

    tvt->state = TS_NONE;
    tvt->bufp = tvt->buflen = 0;
    if(!(he = gethostbyname(host)) || !he->h_addr_list[0] ||
       he->h_addrtype != AF_INET)
      return ERR_NOHOST;
    tvt->fd = socket(PF_INET, SOCK_STREAM, 0);
    tvt->state = TS_OPEN;
    if(tvt->fd < 0)
      return ERR_OS;
    opt = -1;
    setsockopt(tvt->fd, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt));
/*    tvt->addr.in.sin_len = sizeof(tvt->addr.in); */
    tvt->addr.in.sin_family = AF_INET;
    tvt->addr.in.sin_port = htons(port);
    for(p=he->h_addr_list;*p;p++) {
	memcpy((void *)&tvt->addr.in.sin_addr, *p, he->h_length);
	if(!connect(tvt->fd, (struct sockaddr *)&tvt->addr.sa,
		    sizeof(tvt->addr.sa)))
	  break;
    }
    if(!*p)
      return ERR_CONNECT;
    tvt->state = TS_CONNECT;
    return ERR_OK;
}

static void close_tn(struct tvt *tvt)
{
    switch(tvt->state) {
      case TS_CONNECT:
	shutdown(tvt->fd, SHUT_RDWR);
	tvt->state = TS_OPEN;
      case TS_OPEN:
	close(tvt->fd);
	tvt->state = TS_NONE;
    }
}

static int tn_read(struct tvt *tvt, char *buf, int len)
{
    int res, off = 0;
    fd_set fds;

    if(tvt->buflen > tvt->bufp) {
	res = tvt->buflen - tvt->bufp;
	if(res > len)
	  res = len;
	memcpy(buf, tvt->buf, res);
	len -= res;
	off += res;
	tvt->bufp += res;
	if(!len)
	  return ERR_OK;
    }
    do {
	do {
	    errno = 0;
	    FD_ZERO(&fds);
	    FD_SET(tvt->fd, &fds);
	    res = select(tvt->fd + 1, &fds, NULL, NULL, NULL);
	} while(res < 0 && (errno == EINTR || errno == EAGAIN) && !timedout);
	if(res < 0)
	  break;
	errno = 0;
	res = read(tvt->fd, buf+off, len);
	if(res > 0) {
	    off += res;
	    len -= res;
	}
    } while(res && len > 0 && (errno == 0 || errno == EAGAIN || errno == EINTR) && !timedout);
    if(clralarm())
      res = ERR_TO;
    else if(errno)
      res = ERR_OS;
    else
      res = off;
    return res;
}

static char *tn_readln(struct tvt *tvt, char *dst, int maxdlen)
{
    char *s;
    int i, n;
    fd_set fds;

    if(tvt->state != TS_CONNECT) {
	if(debug & (DB_FTPIN | DB_FTPRSP))
	  fprintf(stderr, "readln:  Not Connected\n");
	return NULL;
    }
    if(tvt->bufp == tvt->buflen)
      tvt->bufp = tvt->buflen = 0;
    n = tvt->bufp;
    s = tvt->buf + tvt->bufp;
    do {
	for(i = tvt->buflen - n; i; i--, s++) {
	    if(dst)
	      *dst++ = *s;
	    if(*s == '\n' || (dst && --maxdlen <= 1))
	      break;
	}
	if(i) {
	    if(dst)
	      *dst = 0;
	    else
	      *s = 0;
	    i = s - tvt->buf + 1;
	    if(dst)
	      dst -= i-tvt->bufp;
	    s = tvt->buf + tvt->bufp;
	    tvt->bufp = i;
	    if(debug & DB_FTPIN)
	      fprintf(stderr, "< %s\n", dst?dst:s);
	    return dst?dst:s;
	}
	if(dst) {
	    s = tvt->buf;
	    tvt->bufp = tvt->buflen = 0;
	}
	n = tvt->buflen;
	if(tvt->buflen == TVTBUF && tvt->bufp) {
	    memcpy(tvt->buf, tvt->buf + tvt->bufp, tvt->buflen - tvt->bufp);
	    tvt->buflen -= tvt->bufp;
	    n -= tvt->bufp;
	    s -= tvt->bufp;
	    tvt->bufp = 0;
	}
	if(tvt->buflen == TVTBUF) {
	    tvt->buf[TVTBUF-1] = 0; /* silently truncate */
	    tvt->bufp = tvt->buflen;
	    if(debug & DB_FTPIN)
	      fprintf(stderr, "< %s [truncated]\n", tvt->buf);
	    else if(debug & DB_FTPRSP)
	      fputs("<[truncated]\n", stderr);
	    return tvt->buf;
	}
	errno = 0;
	do {
	    FD_ZERO(&fds);
	    FD_SET(tvt->fd, &fds);
	    i = select(tvt->fd + 1, &fds, NULL, NULL, NULL);
	} while(i < 0 && (errno == EINTR || errno == EAGAIN) && !timedout);
	if(i < 0)
	  break;
	errno = 0;
	i = read(tvt->fd, tvt->buf + tvt->buflen, TVTBUF - tvt->buflen);
	if(i > 0)
	    tvt->buflen += i;
    } while(i && (errno == 0 || errno == EAGAIN || errno == EINTR) && !timedout);
    if(!i && debug & DB_FTPIN)
      fprintf(stderr, "< <<EOF>>");
    if(timedout && debug & (DB_FTPIN | DB_FTPRSP))
      fputs("< [timed out]\n", stderr);
    else if(i && debug & (DB_FTPIN | DB_FTPRSP)) {
	timedout = 1;
	perror("readln: exit");
    }
    return NULL;
}

static int rewrite(int fd, const void *buf, int buflen)
{
    int off = 0, res;
    fd_set wfd;

    do {
	do {
	    FD_ZERO(&wfd);
	    FD_SET(fd, &wfd);
	    res = select(fd + 1, NULL, &wfd, NULL, NULL);
	} while(res < 0 && (errno == EINTR || errno == EAGAIN) && !timedout);
	if(res < 0)
	  break;
	res = write(fd, buf, buflen);
	if(res > 0) {
	    off += res;
	    buflen -= res;
	}
    } while(buflen > 0 && (errno == EAGAIN || errno == EINTR) && !timedout);
    return buflen > 0?ERR_OS:ERR_OK;
}

/* This ONEWR crap has to be here because some sites don't adhere strictly
 * to the statement in rfc-959:
 *    FTP commands are "Telnet strings" terminated by the "Telnet end of
 *    line code".  The command codes themselves are alphabetic characters
 *    terminated by the character <SP> (Space) if parameters follow and
 *    Telnet-EOL otherwise.
 * Specifically, some sites hang on the PORT command if it isn't sent as
 * one transmission.
 * 
 * Since I'm tired of messing with it, I'm just going to blindly reuse
 * the receive buffer.
 */
#define ONEWR

static int tn_writeln2(struct tvt *tvt, const char *str1, const char *str2)
{
#ifndef ONEWR
    int ret;
#endif

    if(tvt->state != TS_CONNECT)
      return ERR_CONNECT;
	if(debug & DB_FTPOUT)
      fprintf(stderr, "> %s%s\n", str1?str1:"", str2?str2:"");
#ifndef ONEWR
    if(str1)
      if((ret = rewrite(tvt->fd, str1, strlen(str1))) != ERR_OK)
	return ret;
    if(str2)
      if((ret = rewrite(tvt->fd, str2, strlen(str2))) != ERR_OK)
	return ret;
    if(str1 || str2)
      rewrite(tvt->fd, "\r\n", 2);
    return ERR_OK;
#else
    if(!str1 && !str2)
      return ERR_OK;
    tvt->buflen = tvt->bufp = 0;
    if(str1)
      strcpy(tvt->buf, str1);
    else
      tvt->buf[0] = 0;
    if(str2)
      strcat(tvt->buf, str2);
    strcat(tvt->buf, "\r\n");
    return rewrite(tvt->fd, tvt->buf, strlen(tvt->buf));
#endif
}

static const struct retries {
    short resp; const char *item; size_t tosend; const char *def;
} retries[] = {
/*    {30, "USER ", offsetof(struct ftpsite, user), "anonymous"}, */
    {31, "PASS ", offsetof(struct ftpsite, pass), NULL},
    {32, "ACCT ", offsetof(struct ftpsite, acct), "ftp"}
};

char ftp_endport(struct ftpsite *site)
{
    char ret = ERR_OK;

    if(site->dfd.fd >= 0) {
	shutdown(site->dfd.fd, SHUT_RDWR);
	close(site->dfd.fd);
	site->dfd.fd = -1;
	ret = ftp_cmd(site, NULL);
    }
    if(site->dport >= 0) {
	close(site->dport);
	site->dport = -1;
    }
    return ret;
}

static char pwbuf[256] = {0};

char ftp_cmd2(struct ftpsite *site, const char *cmd, const char *arg)
{
    char *s;
    short rc, rcls;

    if(!cmd || strcmp(cmd, "QUIT"))
      setalarm(site->to.cmd);
    if(cmd) {
	tn_writeln2(&site->tvt, cmd, arg);
    }
    if(site->dfd.fd >= 0)
      ftp_endport(site);
    for(;;) {
	s = tn_readln(&site->tvt, NULL, 0);
	if(!s) {
	    clralarm();
	    return ERR_TO; /* most likely in any case */
	}
	if(debug & DB_FTPRSP)
	  fprintf(stderr, "< %s\n", s);
	site->lastresp = s;
	if(isdigit(*s) && isdigit(s[1]) && isdigit(s[2]) && !isdigit(s[3]) &&
	   (!s[3] || isspace(s[3]))) {
	    rcls = *s;
	    rc = atoi(s+1);
	    if(rcls > FTP_DONE) { /* CONT/EAGAIN/ERROR */
		const struct retries *rp;
		const char *resp;

		for(rp = retries;rp < (struct retries *)((char *)retries +
							 sizeof(retries));rp++)
		  if(rc == rp->resp && strcmp(cmd, rp->item)) {
		      resp = *(char **)((char *)site+rp->tosend);
		      if(!resp)
			resp = rp->def;
		      if(!resp && !(resp = pwbuf)[0]) {
			  struct passwd *pw;
			  struct hostent *he;
			  size_t len;

			  pw = getpwuid(getuid());
			  if(pw)
			    strncpy(pwbuf, pw->pw_name, 128);
			  len = strlen(pwbuf);
			  pwbuf[len++] = '@';
			  pwbuf[255] = 1;
			  if(gethostname(pwbuf+len, 256-len) || pwbuf[255] != 1 ||
			     !(he = gethostbyname(pwbuf+len)) ||
			     !he->h_addr_list || !he->h_addr_list[0] ||
			     he->h_addrtype != AF_INET ||
			     strlen(he->h_name) > 255 - len)
			    pwbuf[len] = 0;
			  else
			    strcpy(pwbuf+len, he->h_name);
		      }
		      if(rcls == FTP_ERROR) {
			  ftp_cmd2(site, rp->item, resp);
			  rcls = site->lastresp[0];
			  tn_writeln2(&site->tvt, cmd, arg);
		      } else
			tn_writeln2(&site->tvt, rp->item, resp);
		      rp = NULL;
		      break;
		  }
		if(!rp)
		  continue;
	    }
/*	    if(rcls == FTP_EAGAIN) {
		if(cmd)
		  tn_writeln2(&site->tvt, cmd, arg);
		else
		  return clralarm()?ERR_TO:ERR_CMD;
	    } else */
	      return clralarm()?ERR_TO:rcls>=FTP_EAGAIN?ERR_CMD:ERR_OK;
	}
    }
}

#ifdef SOCKS
extern void SOCKSinit(const char *);
#endif

void ftp_init(const char *progname)
{
#ifdef SOCKS
    SOCKSinit(progname);
#endif
}

int ftp_openconn(struct ftpsite *site)
{
    int res;

    if(site->tvt.state == TS_CONNECT)
      return ERR_OK;
    site->dfd.fd = site->dport = -1;
    setalarm(site->to.connect); /* in case name service hangs */
    open_tn(site->host, site->port ? site->port: 21, &site->tvt);
    clralarm();
    if(ftp_cmd2(site, NULL, NULL) != ERR_OK)
      return ERR_CONNECT;
    res = ftp_cmd2(site, "USER ", site->user?site->user:"anonymous"); /* automatically sends PASS & ACCT */
#if 0
    if(res == ERR_OK)
      if((res = ftp_cmd(site, "SYST")) == ERR_TO)
	return ERR_CONNECT;
#endif
    return res;
}

void ftp_closeconn(struct ftpsite *site)
{
    if(site->tvt.state == TS_CONNECT) {
	setalarm(site->to.quit);
	ftp_endport(site);
	ftp_cmd2(site, "QUIT", NULL);
    }
    close_tn(&site->tvt);
    clralarm();
}

int ftp_port(struct ftpsite *site)
{
    struct hostent *he;
    struct utsname un;
    char buf[24];
    char **p;
    unsigned char *a, *pt;
    int opt;
    size_t lopt;
    union sa addr; 

    ftp_endport(site);
    if(uname(&un) < 0)
      return ERR_OS;
    he = gethostbyname(un.nodename);
    if(!he || he->h_addrtype != AF_INET) {
	fputs("Can't find local inet addr",stderr);
	return ERR_NOHOST;
    }
    site->dport = socket(PF_INET, SOCK_STREAM, 0);
    if(site->dport < 0)
      return ERR_OS;
    opt = -1;
    setsockopt(site->dport, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt));
    addr.in.sin_port = 0; /* random port */
    for(p=he->h_addr_list;*p;p++) {
	memcpy(&addr.in.sin_addr.s_addr, *p, he->h_length);
	if(!bind(site->dport, &addr.sa, sizeof(addr)))
	  break;
    }
    if(!*p || listen(site->dport, 1) < 0)
      return ERR_OS;
    lopt = sizeof(addr);
    /* ARRRGGGGGHHH!  Damn Sun and the horse they rode in on! */
    if(getsockname(site->dport, &addr.sa, (void *)&lopt) < 0) {
	close(site->dport);
	site->dport = -1;
	return ERR_OS;
    }
    a = (unsigned char *)&addr.in.sin_addr;
    pt = (unsigned char *)&addr.in.sin_port;
    sprintf(buf, "%u,%u,%u,%u,%u,%u", a[0], a[1], a[2], a[3], pt[0], pt[1]);
    if(ftp_cmd2(site, "PORT ", buf) != ERR_OK) {
	close(site->dport);
	site->dport = -1;
	return ERR_OS;
    }
    return ERR_OK;
}

static int chkdport(struct ftpsite *site)
{
    union sa sa;
    size_t salen = sizeof(sa);

    if(site->dport < 0 && site->dfd.fd < 0)
      return ERR_CONNECT;
    setalarm(site->to.xfer);
    if(site->dfd.fd < 0) {
	site->dfd.bufp = site->dfd.buflen = 0;
	/* see above comment about Sun */
	site->dfd.fd = accept(site->dport, &sa.sa, (void *)&salen);
	if(site->dfd.fd < 0) {
	    clralarm();
	    return ERR_CONNECT;
	}
	site->dfd.state = TS_CONNECT;
	close(site->dport);
	site->dport = -1;
/*	fcntl(site->dfd.fd, F_SETFL, fcntl(site->dfd.fd, F_GETFL) | O_NONBLOCK); */
    }
    return ERR_OK;
}

int ftp_read(struct ftpsite *site, char *buf, int len, char line)
{
    int res;

    res = chkdport(site);
    if(res)
      return -res;
    if(line)
      return tn_readln(&site->dfd, buf, len)?ERR_OK:timedout?ERR_TO:ERR_EOF;
    else
      return tn_read(&site->dfd, buf, len);
}

int ftp_write(struct ftpsite *site, const char *buf, int len)
{
    int res;

    res = chkdport(site);
    if(res)
      return -res;
    res = rewrite(site->dfd.fd, buf, len);
    return res == ERR_OK?len:-res;
}

/* pasv on one site, then port on another to cross-connect */
int ftp_crossconnect(struct ftpsite *site1, struct ftpsite *site2)
{
    unsigned char addr[4];
    char *s;
    unsigned short port;
    int i;
    char buf[24];

    if(ftp_cmd(site1, "PASV") != ERR_OK || atoi(site1->lastresp) != 2 ||
       !(s = strchr(site1->lastresp, '('))) {
	struct ftpsite *tmp;

	tmp = site1;
	site1 = site2;
	site2 = tmp;
	if(ftp_cmd(site1, "PASV") != ERR_OK || atoi(site1->lastresp) != 2 ||
	   !(s = strchr(site1->lastresp, '(')))
	  return ERR_CONNECT;
    }
    for(i=0;i<4;i++) {
	s++;
	addr[i] = strtol(s, &s, 10);
    }
    s++;
    port = strtol(s, &s, 10)*256;
    s++;
    port += strtol(s, NULL, 10);
    sprintf(buf, "%d,%d,%d,%d,%d,%d", addr[0], addr[1], addr[2], addr[3],
	    port>>8,port&0xff);
    if(ftp_cmd2(site2, "PORT ", buf) != ERR_OK)
      return ERR_CONNECT;
    return ERR_OK;
}

/* general support */
/* retrieve remote working directory; assumes connection open */
int ftp_getwd(struct ftpsite *site, char *buf)
{
    const char *s;

    if(ftp_cmd(site, "PWD") != ERR_OK && ftp_cmd(site, "XPWD") != ERR_OK)
      return ERR_CMD;
    s = site->lastresp;
    if(atoi(s) != 257)
      return ERR_CMD;
    s += 4;
    if(*s == '"') {
	while(*++s != '"')
	  *buf++ = *s;
    } else while(!isspace(*s))
	  *buf++ = *s++;
    *buf++ = 0;
    return ERR_OK;
}

/* retrieve modtime of remote file; leaves connection open */
int ftp_filetm(struct ftpsite *site, const char *name, struct tm *tm)
{
    const char *s;
    int i;

    if(ftp_openconn(site) != ERR_OK)
      return(ERR_LOGIN);
    if(ftp_cmd2(site, "MDTM ", name) != ERR_OK ||
       atoi(s = site->lastresp) != 213)
      return ERR_CMD;
    i = sscanf(s,"%*d %4d%2d%2d%2d%2d%2d",&tm->tm_year,&tm->tm_mon,&tm->tm_mday,
	       &tm->tm_hour,&tm->tm_min,&tm->tm_sec);
    if(i != 6)
      return ERR_CMD;
    tm->tm_year-=1900;
    tm->tm_mon--;
    tm->tm_isdst = -1; /* unknown */
    return ERR_OK;
}
