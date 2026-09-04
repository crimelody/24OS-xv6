#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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

  backtrace();   // lab 4: 仅用于测试 bttest，验证 backtrace 输出

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

// lab 4: 设置周期定时警报。sigalarm(interval, handler)：
// interval=0 时关闭；否则每消耗 interval 个 tick，内核跳到用户态 handler。
uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  struct proc *p = myproc();

  if(argint(0, &interval) < 0)
    return -1;
  if(argaddr(1, &handler) < 0)
    return -1;

  p->alarm_interval = interval;
  p->handler_va = handler;
  p->passed_ticks = 0;         // 重置计时
  p->alarm_reentrant = 0;      // 允许新一轮触发
  return 0;
}

// lab 4: 从处理函数返回：恢复被中断的现场（trapframe），
// 使进程在中断发生处继续执行，如同什么都没发生。
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();

  *p->trapframe = p->saved_trapframe;  // 恢复保存的全部寄存器/PC
  p->alarm_reentrant = 0;              // 处理函数结束，允许下次触发
  return p->trapframe->a0;             // 返回值在 a0，保持调用语义
}
