#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//实际上xargs是将自身的第一个参数作为程序，将其他作为参数表，保持这些参数不变的情况下，
//从标准输入一行一行的读取参数，将其作为每次运行（每行一运行）的额外参数

void
run(char *pro, char **args)
{
    //程序运行函数
    if(fork() == 0){
        exec(pro, args);
        exit(0);
    }
    return;
} 

int 
main(int argc, char **argv)
{
    if(argc < 2)
    {
        fprintf(2, "xargs: miss args");
        exit(0);
    }

    char buf[2048]; //标准输入缓冲
    char *p = buf, *arg_head = buf;
    char *argsbuf[128]; //总参数
    char **args = argsbuf; //直接参数
    int i;
    for(i = 1; i < argc; i ++){
        //直接参数赋值
        *args = argv[i];
        args ++;
    }
    char **pa = args; //额外参数指针
    while(read(0, p, 1) != 0){
        if(*p == ' ' || *p == '\n'){
            *p = '\0';

            *(pa++) = arg_head;
            arg_head = p + 1;

            if(*p == '\n'){ 
                //每行要执行一次
                *pa = 0; //标志参数结束
                run(argv[1], argsbuf);
                pa = args; // 重置额外参数，保留直接参数
            }
        }
        p ++;
    }
    if(pa != args){
        //如果最后一行没有换行
        *p = '\0';
        *(pa++) = arg_head;

        *pa = 0;
        run(argv[1], argsbuf);
    }

    while(wait(0) != -1);
    exit(0);
}