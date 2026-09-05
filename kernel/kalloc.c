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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// lab 5: 物理页引用计数。一页可能被多个进程页表引用（COW），
// 索引 = 物理地址 / PGSIZE，只有当计数归零时才真正释放。
int refcnt[PHYSTOP / PGSIZE];
struct spinlock refcnt_lock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcnt_lock, "refcnt");   // lab 5
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // lab 5: 空闲链表初始化时该页"被分配器持有"，引用计 1；
    // 随后 kfree 减到 0 才真正加入 freelist。
    acquire(&refcnt_lock);
    refcnt[(uint64)p / PGSIZE] = 1;
    release(&refcnt_lock);
    kfree(p);
  }
}

// lab 5: 返回物理页当前引用数
int
pageref(void *pa)
{
  int n;
  acquire(&refcnt_lock);
  n = refcnt[(uint64)pa / PGSIZE];
  release(&refcnt_lock);
  return n;
}

// lab 5: 增加物理页引用计数（fork 共享、COW 复制后旧页仍被引用时用）
void
incref(void *pa)
{
  acquire(&refcnt_lock);
  refcnt[(uint64)pa / PGSIZE]++;
  release(&refcnt_lock);
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

  // lab 5: 引用计数减一，仅当无任何引用时才真正回收
  acquire(&refcnt_lock);
  if(refcnt[(uint64)pa / PGSIZE] < 1)
    panic("kfree: refcnt underflow");
  refcnt[(uint64)pa / PGSIZE]--;
  int still_refd = refcnt[(uint64)pa / PGSIZE];
  release(&refcnt_lock);
  if(still_refd > 0)
    return;             // 还有其他进程引用，暂不释放

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    // lab 5: 新分配页引用计数置 1（本分配者）
    acquire(&refcnt_lock);
    refcnt[(uint64)r / PGSIZE] = 1;
    release(&refcnt_lock);
  }
  return (void*)r;
}
