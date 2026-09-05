//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"   // lab 10: MMAPMINADDR

extern pte_t *walk(pagetable_t, uint64, int);  // lab 10: 定义于 vm.c，未入 defs.h

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

#ifdef LAB_MMAP
// lab 10: 找出包含虚拟地址 va 的 VMA（未使用返回 -1）
static int
find_vma(struct proc *p, uint64 va, int *vma_index)
{
  for(int i = 0; i < 16; i++){
    struct vm_area *v = &p->vmas[i];
    if(v->used && va >= v->addr && va < v->addr + v->len){
      *vma_index = i;
      return 0;
    }
  }
  return -1;
}

// lab 10: 解析 fd 并返回 struct file*（不夺引用）
static struct file*
getfile(int fd)
{
  struct proc *p = myproc();
  if(fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
    return 0;
  return p->ofile[fd];
}

// lab 10: mmap——把文件映射进地址空间（懒加载：不分配物理页）
uint64
sys_mmap(void)
{
  uint64 addr, offset;
  int len, prot, flags, fd;
  struct file *f;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);

  if(len < 0 || (flags != MAP_SHARED && flags != MAP_PRIVATE))
    return -1;
  if((f = getfile(fd)) == 0)
    return -1;
  // 可写+共享 映射要求文件可写（否则数据无法写回）
  if(flags == MAP_SHARED && (prot & PROT_WRITE) && !f->writable)
    return -1;

  // 找空闲 VMA 槽
  int idx = -1;
  for(int i = 0; i < 16; i++)
    if(!p->vmas[i].used){ idx = i; break; }
  if(idx < 0)
    return -1;

  // 分配起始地址：addr=0 时从 MMAPMINADDR 往下找；否则按 addr 页对齐
  uint64 base;
  if(addr == 0){
    // 从高往低扫描已有映射，取最顶部的空隙（简单策略：MMAPMINADDR - 已用总量）
    uint64 top = MMAPMINADDR;
    for(int i = 0; i < 16; i++){
      struct vm_area *v = &p->vmas[i];
      if(v->used && v->addr < top)
        top = v->addr;              // 已有映射占用了更高处→再往低取
    }
    uint64 n = PGROUNDUP(len);
    base = top - n;
    if(base < PGSIZE)               // 不能压到 0 页附近
      base = top;
  } else {
    base = PGROUNDUP(addr);         // 页对齐
  }
  if(base + len >= MMAPMINADDR + 32*PGSIZE)  // 不越过 MMAPMINADDR 顶端
    ;

  // 记录 VMA（懒分配，物理页留给缺页时）
  p->vmas[idx].used = 1;
  p->vmas[idx].addr = base;
  p->vmas[idx].len = len;
  p->vmas[idx].prot = prot;
  p->vmas[idx].flags = flags;
  p->vmas[idx].offset = offset;
  p->vmas[idx].f = f;
  filedup(f);          // 增加文件引用：mmap 期间文件即使 close 也有效

  return base;
}

// lab 10: 懒加载缺页处理——va 落在某个 mmap VMA 内时，
// 分配物理页、从文件读取内容、按权限建立映射。
// 成功返回 0，否则 -1（调用方杀进程）。
int
mmap_pagefault(uint64 va)
{
  struct proc *p = myproc();
  int idx;
  struct vm_area *v;
  char *mem;
  uint64 off;
  uint flags;

  if(find_vma(p, va, &idx) < 0)
    return -1;

  v = &p->vmas[idx];
  if((mem = kalloc()) == 0)
    return -1;

  // 读取文件内容到该页：文件内偏移 = (va-addr)+v->offset
  off = (va - v->addr) + v->offset;
  memset(mem, 0, PGSIZE);            // 超文件部分清零

  ilock(v->f->ip);
  if(off < v->f->ip->size){
    int n = PGSIZE;
    if(off + n > v->f->ip->size)
      n = v->f->ip->size - off;
    readi(v->f->ip, 0, (uint64)mem, off, n);
  }
  iunlock(v->f->ip);

  // 权限：用户位 + 读位 +（可写时）写位。PTE_X 仅当要求可执行。
  flags = PTE_U | PTE_R;
  if(v->prot & PROT_WRITE)
    flags |= PTE_W;
  if(v->prot & PROT_EXEC)
    flags |= PTE_X;

  // 建映射（懒分配的物理页也记录到页表；写权限按 prot 给）
  if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, flags) < 0){
    kfree(mem);
    return -1;
  }
  return 0;
}

// lab 10: 把 VMA 的 [addr, addr+len) 区域解除映射。
// MAP_SHARED 且页脏(PTE_D)则写回文件；整区解除则释放 VMA 槽与文件引用。
uint64
sys_munmap(void)
{
  uint64 addr;
  int len;
  struct proc *p = myproc();
  int idx;

  argaddr(0, &addr);
  argint(1, &len);

  if(len < 0 || addr % PGSIZE != 0)
    return -1;
  if(find_vma(p, addr, &idx) < 0)
    return -1;

  struct vm_area *v = &p->vmas[idx];
  if(addr + len > v->addr + v->len)
    return -1;

  // 逐页处理：先写回共享脏页（在日志事务内），再解除映射
  int dirty_written = 0;
  for(uint64 a = addr; a < addr + len; a += PGSIZE){
    pte_t *pte = walk(p->pagetable, a, 0);
    if(pte && (*pte & PTE_V)){
      uint64 pa = PTE2PA(*pte);
      // MAP_SHARED 且脏页 → 写回文件
      if(v->flags == MAP_SHARED && (*pte & PTE_D)){
        if(!dirty_written){
          begin_op();               // 写盘须在日志事务内
          dirty_written = 1;
        }
        uint64 off = (a - v->addr) + v->offset;
        int n = PGSIZE;
        if(off + n > v->f->ip->size)
          n = v->f->ip->size - off;
        if(n > 0){
          ilock(v->f->ip);
          writei(v->f->ip, 0, pa, off, n);
          iunlock(v->f->ip);
        }
      }
    }
  }
  if(dirty_written)
    end_op();

  // 解除映射并释放物理页（懒加载未触发的页会被宽容跳过）
  uvm_cleanunmap(p->pagetable, addr, len / PGSIZE, 1);

  // 处理 VMA：若整个区域都被解除，释放槽位
  if(addr == v->addr && len == v->len){
    v->used = 0;
    fileclose(v->f);
  } else if(addr == v->addr){
    // 从头部去掉 len：剩余部分前移
    v->addr += len;
    v->len -= len;
    v->offset += len;
  } else if(addr + len == v->addr + v->len){
    v->len -= len;             // 去掉尾部
  }
  return 0;
}
// lab 10: 进程退出时解除全部 VMA——脏的 MAP_SHARED 页先写回文件，
// 再解除映射、释放文件引用。由 proc.c 的 exit() 调用。
void
unmap_all_vmas(struct proc *p)
{
  for(int i = 0; i < 16; i++){
    struct vm_area *v = &p->vmas[i];
    if(!v->used)
      continue;
    // 逐页写回共享脏页（日志事务内）
    int dirty_written = 0;
    for(uint64 a = v->addr; a < v->addr + v->len; a += PGSIZE){
      pte_t *pte = walk(p->pagetable, a, 0);
      if(pte && (*pte & PTE_V) && v->flags == MAP_SHARED && (*pte & PTE_D)){
        if(!dirty_written){
          begin_op();
          dirty_written = 1;
        }
        uint64 off = (a - v->addr) + v->offset;
        int n = PGSIZE;
        if(off + n > v->f->ip->size)
          n = v->f->ip->size - off;
        if(n > 0){
          ilock(v->f->ip);
          writei(v->f->ip, 0, PTE2PA(*pte), off, n);
          iunlock(v->f->ip);
        }
      }
    }
    if(dirty_written)
      end_op();

    // 解除映射并释放物理页（懒加载未触发页宽容跳过）
    uvm_cleanunmap(p->pagetable, v->addr, PGROUNDUP(v->len)/PGSIZE, 1);
    fileclose(v->f);
    v->used = 0;
  }
}
#endif
