#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static struct timespec sleeptime =
{ .tv_sec = 0
, .tv_nsec = 50 * 1000 * 1000 /*50ms*/
};

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  if (argc > 1) {
    sleeptime.tv_nsec = 1000*1000*atoi(argv[1]);
  }

  while (1) {
    char line[4096];
    if (fgets(line, 4096, stdin) == NULL) return 1;
    fputs(line, stdout);
    nanosleep(&sleeptime, NULL);
  }
}
