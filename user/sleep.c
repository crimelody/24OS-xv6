#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 用法：sleep <ticks>，必须恰好一个参数
  if(argc != 2){
    fprintf(2, "usage: sleep <ticks>\n");
    exit(1);
  }

  // 命令行参数是字符串，用 atoi 转成整数 tick 数，然后调用系统调用 sleep
  sleep(atoi(argv[1]));
  exit(0);
}
