#include "kernel/types.h"
#include "user/user.h"

// 思路：每个筛子进程读入一串整数，其中第一个数一定是素数，
// 先打印它，再把后续不能被它整除的数通过新管道传给下一个筛子进程。
void
sieve(int left[2])
{
  int prime, n;
  int right[2];   // 通向下一个筛子进程的管道

  close(left[1]); // left 只用于读，它的写端在调用方已经关闭

  // 先读一个数。若管道已关闭导致 read 返回 0，说明整条链结束，退出
  if(read(left[0], &prime, sizeof(int)) != sizeof(int)){
    close(left[0]);
    exit(0);
  }
  printf("prime %d\n", prime);

  if(pipe(right) < 0){
    fprintf(2, "sieve: pipe failed\n");
    exit(1);
  }

  if(fork() == 0){
    // 子进程成为下一个筛子，只需要从 right 读数据
    close(right[1]); // 子进程不写 right，关闭写端
    close(left[0]);  // 左侧的数据已经处理完，不再需要
    sieve(right);
    exit(0);         // 递归返回后不再继续执行，这里兜底退出
  } else {
    // 本进程负责过滤：把不能被 prime 整除的数写入 right
    close(right[0]); // 本进程只写 right，关闭读端
    while(read(left[0], &n, sizeof(int)) == sizeof(int)){
      if(n % prime != 0)
        write(right[1], &n, sizeof(int));
    }
    close(left[0]);
    close(right[1]); // 写完关闭写端，下游 read 才会返回 0 而结束
    wait(0);         // 等整条链结束后本进程再退出
    exit(0);
  }
}

int
main(void)
{
  int p[2];
  pipe(p);

  if(fork() == 0){
    // 子进程成为第一个筛子，从 p 读数据
    close(p[1]);
    sieve(p);
    exit(0);
  } else {
    // 祖先进程：把 2 到 35 全部写入管道，作为筛子的输入
    close(p[0]);
    for(int i = 2; i <= 35; i++)
      write(p[1], &i, sizeof(int));
    close(p[1]);   // 写完关闭写端，整条筛子链才能逐级结束
    wait(0);
    exit(0);
  }
}
