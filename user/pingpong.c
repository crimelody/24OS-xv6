#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 两条单向管道：
  //   p2c[0..1]: 父进程 -> 子进程
  //   c2p[0..1]: 子进程 -> 父进程
  int p2c[2], c2p[2];
  char byte = 'x';
  int pid;

  pipe(p2c);
  pipe(c2p);

  pid = fork();
  if(pid < 0){
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // ===== 子进程：收 ping，回 pong =====
    close(p2c[1]);   // 不需要向父进程方向写
    close(c2p[0]);   // 不需要从子->父管道读

    read(p2c[0], &byte, 1);                 // 阻塞等父进程发来 1 字节
    printf("%d: received ping\n", getpid()); // 打印本进程 pid
    write(c2p[1], &byte, 1);                // 把字节回传给父进程

    close(p2c[0]);
    close(c2p[1]);
    exit(0);
  } else {
    // ===== 父进程：发 ping，收 pong =====
    close(p2c[0]);   // 不需要从父->子管道读
    close(c2p[1]);   // 不需要向子->父管道写

    write(p2c[1], &byte, 1);   // 发 1 字节给子进程
    close(p2c[1]);             // 关闭写端，让子进程 read 能结束（关键）
    read(c2p[0], &byte, 1);    // 等子进程的回应
    printf("%d: received pong\n", getpid());

    close(c2p[0]);
    wait(0);        // 回收子进程，避免僵尸
    exit(0);
  }
}
