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

#define NBUCKETS 13 // 建议使用素数

struct {
  struct spinlock lock[NBUCKETS]; // 每个哈希桶一把锁
  struct buf buf[NBUF];           // 实际的 buffer 内存池
  struct buf buckets[NBUCKETS];   // 每个桶的链表头
  struct spinlock alloc_lock;     // 新增：全局分配锁 (用于序列化分配过程)
} bcache;

// 哈希函数
int bhash(int dev, uint blockno) {
  return (dev + blockno) % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;
  char lockname[16];

  // 1. 初始化全局分配锁
  initlock(&bcache.alloc_lock, "bcache_alloc");

  // 2. 初始化每个桶的锁和链表头
  for(int i = 0; i < NBUCKETS; i++){
    snprintf(lockname, sizeof(lockname), "bcache_%d", i);
    initlock(&bcache.lock[i], lockname);
    // 建立循环链表
    bcache.buckets[i].next = &bcache.buckets[i];
    bcache.buckets[i].prev = &bcache.buckets[i];
  }

  // 3. 将所有空闲块初始放入 bucket[0] (或者分散放入，这里放入 bucket[0] 简单些)
  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    b->refcnt = 0;
    b->timestamp = 0;
    initsleeplock(&b->lock, "buffer");
    
    // 插入 bucket[0]
    b->next = bcache.buckets[0].next;
    b->prev = &bcache.buckets[0];
    bcache.buckets[0].next->prev = b;
    bcache.buckets[0].next = b;
  }
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int id = bhash(dev, blockno);

  // 阶段 1: 只锁当前桶
  acquire(&bcache.lock[id]);

  for(b = bcache.buckets[id].next; b != &bcache.buckets[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.lock[id]);

  // 阶段 2: 全局替换/分配流程 (持有 alloc_lock)

  acquire(&bcache.alloc_lock);
  acquire(&bcache.lock[id]);

  // 再次检查缓存是否存在
  // 因为在释放锁和获取 alloc_lock 之间，可能别的进程已经把块加载进来了
  for(b = bcache.buckets[id].next; b != &bcache.buckets[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      release(&bcache.alloc_lock); 
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 未命中。寻找替代块 (Victim)
  // 策略：找 refcnt==0 且 timestamp 最小的块
  struct buf *victim = 0;
  int victim_bucket = -1;
  uint min_tick = 0xffffffff; 

  // // 先在当前桶找 (这一步不需要额外的锁，因为已经持有 lock[id])
  // for(b = bcache.buckets[id].next; b != &bcache.buckets[id]; b = b->next){
  //   if(b->refcnt == 0 && b->timestamp < min_tick){
  //     victim = b;
  //     min_tick = b->timestamp;
  //     victim_bucket = id;
  //   }
  // }
  
  // // 如果当前桶找到了，直接复用，不需要去别的桶
  // if(victim){
  //   goto found; 
  // }

  //当前桶没找到，去其他桶找
  for(int i = 0; i < NBUCKETS; i++){
    if(i == id) continue; 

    acquire(&bcache.lock[i]); 

    for(b = bcache.buckets[i].next; b != &bcache.buckets[i]; b = b->next){
      if(b->refcnt == 0 && b->timestamp < min_tick){
        // 找到一个更旧的就更新记录
        victim = b;
        min_tick = b->timestamp;
        victim_bucket = i;
      }
    }
    
    if(victim && victim_bucket == i) {
      // 在这个桶找到了，将其从该桶移除
      victim->next->prev = victim->prev;
      victim->prev->next = victim->next;
      release(&bcache.lock[i]);
      
      // 加入到当前桶 id (我们持有 lock[id])
      victim->next = bcache.buckets[id].next;
      victim->prev = &bcache.buckets[id];
      bcache.buckets[id].next->prev = victim;
      bcache.buckets[id].next = victim;
      
      goto found;
    }
    
    release(&bcache.lock[i]); 
  }

  panic("bget: no buffers");

found:
  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  
  release(&bcache.lock[id]);
  release(&bcache.alloc_lock); 
  acquiresleep(&victim->lock);
  return victim;
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

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = bhash(b->dev, b->blockno);
  
  acquire(&bcache.lock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks; // 记录释放时间戳
  }
  release(&bcache.lock[id]);
}

void
bpin(struct buf *b) {
  int id = bhash(b->dev, b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id = bhash(b->dev, b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


