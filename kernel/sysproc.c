#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

extern pte_t *walk(pagetable_t, uint64, int);   // lab 3: 定义于 vm.c，未入 defs.h

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
// lab 3: 检查用户地址 base 起 len 个页中哪些页被访问过（PTE_A 位），
// 结果以位图写回用户地址 mask（第 i 页对应第 i 位）。检查后清除 A 位。
uint64
sys_pgaccess(void)
{
  uint64 base;        // 起始虚拟地址（第 0 参）
  int len;            // 页数（第 1 参）
  uint64 maskaddr;    // 用户态位图缓冲区地址（第 2 参）
  struct proc *p = myproc();
  uint64 maskbits = 0;
  pte_t *pte;

  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &maskaddr);

  // 位图是一个 uint64，最多覆盖 64 页
  if(len > 64 || len < 0)
    return -1;

  for(int i = 0; i < len; i++){
    if((pte = walk(p->pagetable, base + i * PGSIZE, 0)) == 0)
      return -1;           // 该页未映射
    if(*pte & PTE_A){
      maskbits |= (1L << i);   // 第 i 页被访问过 → 第 i 位置 1
      *pte &= ~PTE_A;          // 清除 A 位，供下一次检测
    }
  }

  // 把位图拷回用户空间
  if(copyout(p->pagetable, maskaddr, (char *)&maskbits, sizeof(maskbits)) < 0)
    return -1;
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
