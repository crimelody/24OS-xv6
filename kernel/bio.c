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

struct {
  struct buf buf[NBUF];
  // lab 8: 哈希分桶。每个桶是独立 LRU 链表 + 独立锁，
  // 不同块号的访问只竞争同一桶内的锁，大幅降低全局 bcache.lock 争用。
  struct bucket {
    struct spinlock lock;
    struct buf head;      // 桶内 LRU 哨兵：head.next 最近用，head.prev 最久
  } bucket[NBUCKET];
} bcache;

// 块号 → 桶号（NBUCKET 取质数使分布更均匀）
static struct bucket *
hash_bucket(uint dev, uint blockno)
{
  return &bcache.bucket[blockno % NBUCKET];
}

void
binit(void)
{
  struct buf *b;
  struct bucket *bk;

  for(int i = 0; i < NBUCKET; i++){
    bk = &bcache.bucket[i];
    initlock(&bk->lock, "bcache");
    bk->head.prev = &bk->head;
    bk->head.next = &bk->head;
  }

  // 初始化时把所有缓冲挂到桶 0；运行时 bget 会把用到的缓冲
  // 搬去各自哈希桶，桶 0 的空闲缓冲逐渐被"偷"光后由跨桶回收兜底。
  bk = &bcache.bucket[0];
  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->next = bk->head.next;
    b->prev = &bk->head;
    bk->head.next->prev = b;
    bk->head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct bucket *bk = hash_bucket(dev, blockno);

  acquire(&bk->lock);

  // ① 本桶查缓存命中
  for(b = bk->head.next; b != &bk->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bk->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // ② 本桶有未使用缓冲则直接回收（不跨桶，减少锁竞争）
  for(b = bk->head.prev; b != &bk->head; b = b->prev){
    if(b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      // 移到桶头（最近使用）
      b->next->prev = b->prev;
      b->prev->next = b->next;
      b->next = bk->head.next;
      b->prev = &bk->head;
      bk->head.next->prev = b;
      bk->head.next = b;
      release(&bk->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bk->lock);

  // ③ 本桶满了：到其他桶偷一个空闲缓冲
  for(int i = 1; i < NBUCKET; i++){
    struct bucket *t = &bcache.bucket[(blockno + i) % NBUCKET];
    acquire(&t->lock);
    for(b = t->head.prev; b != &t->head; b = b->prev){
      if(b->refcnt == 0){
        b->next->prev = b->prev;      // 从 t 桶摘除
        b->prev->next = b->next;
        release(&t->lock);

        // 初始化并放入本桶
        acquire(&bk->lock);
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->next = bk->head.next;
        b->prev = &bk->head;
        bk->head.next->prev = b;
        bk->head.next = b;
        release(&bk->lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&t->lock);
  }

  panic("bget: no buffers");
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

  // lab 8: 释放操作按缓冲块号定位到对应桶加锁
  struct bucket *bk = hash_bucket(b->dev, b->blockno);
  acquire(&bk->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it. move to the head (most recently used).
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bk->head.next;
    b->prev = &bk->head;
    bk->head.next->prev = b;
    bk->head.next = b;
  }
  
  release(&bk->lock);
}

void
bpin(struct buf *b) {
  struct bucket *bk = hash_bucket(b->dev, b->blockno);
  acquire(&bk->lock);
  b->refcnt++;
  release(&bk->lock);
}

void
bunpin(struct buf *b) {
  struct bucket *bk = hash_bucket(b->dev, b->blockno);
  acquire(&bk->lock);
  b->refcnt--;
  release(&bk->lock);
}


