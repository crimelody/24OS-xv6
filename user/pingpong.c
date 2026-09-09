#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 建立两条方向相反的管道，让父子进程各用一条来发送数据：
  //   p2c：父进程写、子进程读
  //   c2p：子进程写、父进程读
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
    // 子进程：先接收 ping，再回发 pong
    close(p2c[1]);   // 子进程不往 p2c 写，关闭写端
    close(c2p[0]);   // 子进程不从 c2p 读，关闭读端

    read(p2c[0], &byte, 1);                  // 阻塞等待父进程发来 1 字节
    printf("%d: received ping\n", getpid()); // 打印自己的 pid
    write(c2p[1], &byte, 1);                 // 把收到的字节原样回发给父进程

    close(p2c[0]);
    close(c2p[1]);
    exit(0);
  } else {
    // 父进程：先发送 ping，再接收 pong
    close(p2c[0]);   // 父进程不从 p2c 读，关闭读端
    close(c2p[1]);   // 父进程不往 c2p 写，关闭写端

    write(p2c[1], &byte, 1);   // 发送 1 字节给子进程
    close(p2c[1]);             // 关闭写端后，子进程的 read 才能返回
    read(c2p[0], &byte, 1);    // 等待子进程回发
    printf("%d: received pong\n", getpid());

    close(c2p[0]);
    wait(0);        // 等待子进程结束，避免留下僵尸进程
    exit(0);
  }
}
