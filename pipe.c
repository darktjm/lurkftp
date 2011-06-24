#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

char buf[4096];

int main(int argc, char **argv)
{
    FILE *f;
    int i;
    char bg = 0;

    if(argc < 2) {
	fprintf(stderr, "pipe: make pipe seekable: %s <cmd>\n", argv[0]);
	exit(1);
    }
    if(argc > 2 && !strcmp(argv[1], "-b")) {
	argv++;
	argc--;
	bg = 1;
    }
    f = tmpfile();
    while((i = fread(buf, 1, 4096, stdin)) > 0)
      if(fwrite(buf, i, 1, f) != 1) {
	  perror("tmp file write");
	  exit(1);
      }
    rewind(f);
    close(0);
    dup2(fileno(f), 0);
    close(fileno(f));
    execvp(argv[1], argv+1);
    perror("exec");
    exit(1);
}
