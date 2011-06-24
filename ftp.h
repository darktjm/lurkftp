#include <stdlib.h> /* mainly for sys/types.h on Sun */
#include <sys/socket.h>
#include <time.h>
#include <netinet/in.h>

#ifdef SOCKS
#define connect Rconnect
#define getsockname Rgetsockname
#define bind Rbind
#define accept Raccept
#define listen Rlisten
#define select Rselect
#endif

extern int debug;

#define DB_FTPOUT 1
#define DB_FTPIN  2
#define DB_FTPRSP 4

enum {
    ERR_OK, ERR_NOHOST, ERR_CONNECT, ERR_TO, ERR_LOGIN, ERR_OS, ERR_MEM,
    ERR_CMD, ERR_EOF, ERR_MAX
};

union sa {
    struct sockaddr_in in;
    struct sockaddr    sa;
};

#define TVTBUF 4096

struct tvt {
    union sa addr;
    int fd;
    unsigned char state;
    unsigned char flags;
    unsigned short buflen, bufp;
    char buf[TVTBUF];
};

enum {
    TS_NONE, TS_OPEN, TS_CONNECT
};

enum {
    FTP_READY = '1', FTP_DONE, FTP_MORE, FTP_EAGAIN, FTP_ERROR
};

struct to {
    unsigned short retrycnt, retrytime, connect, quit, xfer, cmd;
};

extern struct to defto; /* where to get default timeouts from */

struct ftpsite {
    const char *host;
    const char *user;
    const char *pass;
    const char *acct;
    unsigned short port;
    struct to to;
    const char *lastresp;
    char passive; /* flag: try passive connection before port */
    /* private info */
    struct tvt tvt; /* control connection */
    struct tvt dfd; /* data conection */
    int dport; /* port command */
};

/* FTP stuff */
char ftp_cmd2(struct ftpsite *site, const char *cmd, const char *arg);
#define ftp_cmd(s,c) ftp_cmd2(s,c,NULL)
char ftp_endport(struct ftpsite *site);
int ftp_openconn(struct ftpsite *site);
void ftp_closeconn(struct ftpsite *site);
int ftp_port(struct ftpsite *site, char try_passive);
int ftp_passive(struct ftpsite *site, char try_port);
#define ftp_dopen(site) ((site)->passive?ftp_passive(site,1):ftp_port(site,1))
int ftp_read(struct ftpsite *site, char *buf, int len, char line);
int ftp_write(struct ftpsite *site, const char *buf, int len);
void ftp_init(const char *progname);

/* general support */
#define ftp_type(s, fmt) ftp_cmd2(s, "TYPE ", fmt)
int ftp_getwd(struct ftpsite *site, char *buf);
#define ftp_chdir(site, dir) ftp_cmd2(site, "CWD ", dir)
int ftp_filetm(struct ftpsite *site, const char *name, struct tm *tm);
