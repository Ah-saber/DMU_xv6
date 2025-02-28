#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv)
{
  int p1[2], p2[2];
  pipe(p1); pipe(p2);

  if(fork() != 0)
  {
    write(p1[1], "o", 1);
    char buf;
    read(p2[0], &buf, 1);
    printf("%d: received pong\n", getpid());
    wait(0);

  }
  else
  {
    char buf;
    read(p1[0], &buf, 1);
    printf("%d: received ping\n", getpid());

    write(p2[1], &buf, 1);

  }
  exit(0);
}
