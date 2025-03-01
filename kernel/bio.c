// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"


#define NBUFMAP_BUCKET 13 //规定一个素数值来做大小，减少冲突概率，实际上只有8块block
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)


extern uint ticks;

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;

// } bcache;

struct {
  struct buf buf[NBUF];
  //struct spinlock eviction_lock;

  // Hash map: dev and blockno to buf
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];
} bcache;

void
binit(void)
{
  // Initialize bufmap
  for(int i=0;i<NBUFMAP_BUCKET;i++) {
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }

  // Initialize buffers
  for(int i=0;i<NBUF;i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    // put all the buffers into bufmap[0]
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }

  //initlock(&bcache.eviction_lock, "bcache_eviction");
}

int  //查看是否是前半
lockcheck(int key, int j)
{
  int half = NBUFMAP_BUCKET / 2;
  if(j == 0) return 1;
  if(key >= half && j + half < key){
    return 0;
  }else if(key < half && (j < (key - half + NBUFMAP_BUCKET) % NBUFMAP_BUCKET && j > key)){
    return 0;
  }else  
    return 1;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
//依据块号得到哈希值，对对应哈希值的桶上锁，在桶里找块，
  uint key = BUFMAP_HASH(dev, blockno); //获得hash key
  acquire(&bcache.bufmap_locks[key]);  //先得到锁

  b = bcache.bufmap[key].next;
  while(b != 0){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt ++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }

  // release(&bcache.bufmap_locks[key]);  //可以放锁，此时需要将后续驱逐序列化，即增加驱逐锁

  // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }


  //维护时间戳，但是怎么不冲突
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  //在对应桶里找不到，驱逐并加到自己桶里
  //限定范围为一半的桶，防止死锁
  int i;
  int lm = -1;
  struct buf *before_b = 0; //维护一个前序节点，用来后面删掉取出的buf
  int mm = -1;
  for(i = 0; i < NBUFMAP_BUCKET; i ++){
    if(i == key || !lockcheck(key, i)) continue;
    uint nid = i;  //保证在循环右侧
    acquire(&bcache.bufmap_locks[nid]); //获得锁
    struct buf *nb = bcache.bufmap[nid].next;
    struct buf *pnb = &bcache.bufmap[nid];
    while(nb){
      if(nb->lastuse < mm || mm == -1){
        if(lm != nid){
          if(lm != -1) release(&bcache.bufmap_locks[lm]); //如果当前最小变了，那就释放锁
          lm = nid;
          before_b = pnb;
          b = nb;
        }else{ //否则就是一个桶内的更新
          before_b = pnb;
          b = nb;
        }
      }
      pnb = nb;
      nb = nb->next;
    }
    if(lm != nid && holding(&bcache.bufmap_locks[nid])) release(&bcache.bufmap_locks[nid]); //如果不是最小的那个，那要在过程中释放锁
  }

  if(lm != -1){
    //找得到一个最小的，转到key的哈希桶中,此时不会获得自己的id，所以不会发生将新旧桶装一起的情况
    //从原本的链中删除
    before_b->next = b->next;
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
    release(&bcache.bufmap_locks[lm]); //放掉所有锁，先放lm再放自己，防止刚拿到的buf被人拿走，

    b->dev = dev;
    b->blockno = blockno;
    b->refcnt = 1;
    b->valid = 0;  //无效

    release(&bcache.bufmap_locks[key]);
    acquiresleep(&b->lock);
    return b;
  }
  //找不到

  panic("bget: no buffers");
}


// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
  panic("brelse");
  
  releasesleep(&b->lock);
  
  int key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    b->lastuse = ticks;  //保存最后使用时间
  }
  
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  int key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  int key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}
