#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void pipeline(int pleft[2]){
  //先读第一个数，是质数
  int x;
  read(pleft[0], &x, sizeof(x));

  if(x == -1) exit(0);

  printf("prime %d\n", x);

  int t = x;

  int pright[2];
  pipe(pright);
  if(fork() == 0){
    //父进程
    //close(pleft[1]); 出错点，因为执行顺序不一定，先关闭这个会导致上一个进程的输入端关闭，无法输入
    close(pright[0]);

    while(read(pleft[0], &x, sizeof(x)) && x != -1){

      if(x % t != 0) {
        write(pright[1], &x, sizeof(x));
      }
    }
    x = -1;
    write(pright[1], &x, sizeof(x));
    wait(0);
    exit(0);
  }else
  {
    //子进程
    close(pleft[0]);
    //close(pleft[1]);
    close(pright[1]);
    pipeline(pright);

  }
}

int main(int argc, char** argv){

  int pinit[2];
  pipe(pinit);

  if(fork() == 0){
    //子进程
    close(pinit[1]);
    pipeline(pinit);
    exit(0);
  }
  else{
    close(pinit[0]);
    
    int i;
    for(i = 2; i < 36; i ++){
      
      write(pinit[1], &i, sizeof(i));

    }

    i = -1;
    write(pinit[1], &i, sizeof(i));
    wait(0);
    exit(0);
  
  }
}

