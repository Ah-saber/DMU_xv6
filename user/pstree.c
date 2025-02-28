#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/riscv.h"
#include "kernel/spinlock.h"
#include "kernel/proc.h"

struct proc p[NPROC];

void 
pftree(struct proc *pp, int depth)
{
    int i;
    for(i = 0; i < depth; i ++){
        printf("    ");
    }
    printf("%s(%d)\n", pp->name, pp->pid);

    struct proc *child;
    for(child = p; child < &p[NPROC]; child ++){
        if(child->state != UNUSED && child->ppid && child->ppid == pp->pid){
            pftree(child, depth + 1);
        }
    }
}


int
main(int argc, char **argv)
{
    if(ps(p) < 0){
        fprintf(2, "ps: failed\n");
        exit(1);
    }
    struct proc *pp;
    //struct proc *init = p;
    //printf("%8s    %8s      %8s     %8s\n", "PID", "STATE", "MEM", "NAME");
    for(pp = p; pp < &p[NPROC]; pp ++){
        if(pp->state != UNUSED && pp->pid == 1){
            //init = pp;
            pftree(pp, 0);
            break;
        }
    }

    exit(0);
}