// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2IDX(pa) ((((uint64)(pa))-KERNBASE) / PGSIZE)
#define ARRLEN PA2IDX(PHYSTOP)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct spinlock cowcount_lock; // cow count array lock
int cowcount[ARRLEN]; // cow count array

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&cowcount_lock, "cowcount");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
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

  acquire(&cowcount_lock);
  if(-- cowcount[PA2IDX(pa)] <= 0) { // avoid cowcount race condition
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&cowcount_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk

  //  acquire(&cowcount_lock); // why fail at here?
    cowcount[PA2IDX(r)] = 1;
  //  release(&cowcount_lock);
  }
  return (void*)r;
}

void* cow_copy_pa(void* pa) {
  acquire(&cowcount_lock); // avoid cowcount race condition

  if(cowcount[PA2IDX(pa)] <= 1) { // ref count is 1, no need to copy
    release(&cowcount_lock);
    return pa;
  }

  char* mem = (char*)kalloc(); // fail here?
  if(mem == 0) {
    release(&cowcount_lock);
    return 0; // out of memory
  }
  
  memmove((void*)mem, (void*)pa, PGSIZE); 
  cowcount[PA2IDX(pa)] --; // decrase                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          

  release(&cowcount_lock);

  return (void*)mem;
}

void cowcount_add(void* pa) { // add ref count by pa
  acquire(&cowcount_lock);
  cowcount[PA2IDX(pa)] ++;
  release(&cowcount_lock);
}