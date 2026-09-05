// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// lab 8: 每个 CPU 一条独立空闲链表 + 独立锁，
// 消除多核争用同一把 kmem.lock 的竞争。
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

// 内部工具：把一页挂到第 id 个 CPU 的链表（调用方持锁）。
static void
push_free(int id, struct run *r)
{
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
}

void
kinit()
{
  struct run *r;

  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");

  // 先由 CPU0 持有全部空闲内存；其他 CPU 需要时再从它偷。
  acquire(&kmem[0].lock);
  r = (struct run*)PGROUNDUP((uint64)end);
  for(; (uint64)r + PGSIZE <= PHYSTOP; r = (struct run*)((uint64)r + PGSIZE)){
    push_free(0, r);
  }
  release(&kmem[0].lock);
}

// Free the page of physical memory pointed at by v.
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP){
    printf("kfree bad pa=%p end=%p PHYSTOP=%p\n", pa, end, (void*)PHYSTOP);
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  // 释放到"当前 CPU"的链表：关中断读 cpuid，避免释放途中被调度走
  push_off();
  int id = cpuid();
  pop_off();

  r = (struct run*)pa;

  acquire(&kmem[id].lock);
  push_free(id, r);
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
void *
kalloc(void)
{
  struct run *r;

  // 关中断读 cpuid：保证整段操作与"本 CPU"绑定
  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // 本 CPU 链表空：轮询其他 CPU "偷"一页
  if(!r){
    for(int other = 0; other < NCPU && other == id; other++){}
    for(int i = 0; i < NCPU; i++){
      int other = i;         // 从 CPU0 开始偷，均摊到各 CPU
      if(other == id)
        continue;
      acquire(&kmem[other].lock);
      if(kmem[other].freelist){
        r = kmem[other].freelist;
        kmem[other].freelist = r->next;
        release(&kmem[other].lock);
        break;
      }
      release(&kmem[other].lock);
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
