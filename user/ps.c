#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/riscv.h"
#include "kernel/spinlock.h"
#include "kernel/proc.h"

struct proc p[NPROC]; //挪到外面，否则栈空间不够
int
main(int argc, char **argv)
{
    if(ps(p) < 0){
        fprintf(2, "ps: failed\n");
        exit(1);
    }
    struct proc *pp;
    printf("%8s    %8s      %8s     %8s\n", "PID", "STATE", "MEM", "NAME");
    for(pp = p; pp < &p[NPROC]; pp ++){
        if(pp->state != UNUSED)
            printf("%8d    %8d      %8d     %8s\n", pp->pid, pp->state, pp->sz, pp->name);
    }

    exit(0);
}