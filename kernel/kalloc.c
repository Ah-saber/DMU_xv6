// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

//思路是每个CPU分一个空闲列表
// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];


// void
// kinit()
// {
//   initlock(&kmem.lock, "kmem");
//   freerange(end, (void*)PHYSTOP);
// }
void
kinit()
{
  int id;
  char *num = "0123456789";
  for(id = 0; id < NCPU; id ++){
    char name[10] = "kmem_cpu_";
    name[9] = num[id];
    initlock(&kmem[id].lock, name);
  }
  freerange(end, (void*)PHYSTOP); //end是kernel之后的第一个地址
}

void
freerange(void *pa_start, void *pa_end)
{
  //好像是把所有的内存都初始化
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) //按页free
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //获得当前CPU，也就是运行freerange的CPU
  push_off();
  int id = cpuid();
  
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  //在这里实施窃取大概
  if(r)
    kmem[id].freelist = r->next;
  else{
    int i;
    for(i = 0; i < NCPU; i ++){
      if(i == id) continue;

      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if(r){
        kmem[i].freelist = r->next; // 先改好被偷的指针指向下一个
        r->next = kmem[id].freelist; //偷到当前列表中，而原来的freelist已经指到末尾了，不再修改
        release(&kmem[i].lock);
        break;
      }
      else release(&kmem[i].lock);
    }
  }
  release(&kmem[id].lock);

  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
