#include "kernel/types.h"
#include "user/user.h"

// 每个进程就是一个"筛子"：
// 从左侧管道读入一串整数，第一个数一定是素数，打印它；
// 把后续数字中不能被该素数整除的，通过新管道转发给下一个进程。
void
sieve(int left[2])
{
  int prime, n;
  int right[2];   // 本进程通向"下一个筛子"的管道

  close(left[1]); // left 是只读的（写端在调用方已关闭）

  // 读第一个数：若是素数链的终点，管道关闭会返回 0，则退出
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
    // 子进程 = 下一个筛子：只需读 right，读端是 right[0]
    close(right[1]); // 子进程不写
    close(left[0]);  // 子进程不再需要左边的数据
    sieve(right);
    exit(0);         // 递归内部会 exit，此处兜底
  } else {
    // 本进程：过滤 left 中剩余的数，写入 right 给下一个筛子
    close(right[0]); // 本进程不读 right
    while(read(left[0], &n, sizeof(int)) == sizeof(int)){
      if(n % prime != 0)
        write(right[1], &n, sizeof(int));
    }
    close(left[0]);
    close(right[1]); // 关键：写完必须关闭写端，下游 read 才会返回 0 而退出
    wait(0);         // 等整条链结束后自己再退出
    exit(0);
  }
}

int
main(void)
{
  int p[2];
  pipe(p);

  if(fork() == 0){
    // 第一个筛子：从 p 读
    close(p[1]);
    sieve(p);
    exit(0);
  } else {
    // 祖先进程：把 2..35 灌入管道
    close(p[0]);
    for(int i = 2; i <= 35; i++)
      write(p[1], &i, sizeof(int));
    close(p[1]);   // 写完关闭，链条才能逐级终止
    wait(0);
    exit(0);
  }
}
