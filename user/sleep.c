#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 检查参数个数：sleep 只接受一个 tick 数参数
  if(argc != 2){
    fprintf(2, "usage: sleep <ticks>\n");
    exit(1);
  }

  // 参数以字符串形式给出，先转成整数，再调用系统调用 sleep
  sleep(atoi(argv[1]));
  exit(0);
}
