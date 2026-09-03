#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

// xargs command [arg ...]
// 逐行从标准输入读取，把每一行按空白拆成若干参数，
// 追加在固定参数后面，fork + exec 执行一次命令。
int
main(int argc, char *argv[])
{
  char buf[512];
  int len = 0;          // 当前行已积累的字符数
  char ch;
  char *args[MAXARG];   // exec 用的完整参数表
  int fixed, i;

  if(argc < 2){
    fprintf(2, "usage: xargs command [arg ...]\n");
    exit(1);
  }

  // 前 argc-1 个参数来自命令行（固定部分）
  fixed = argc - 1;
  for(i = 0; i < fixed; i++)
    args[i] = argv[i + 1];

  // 逐字符读取标准输入，直到出现换行符
  while(read(0, &ch, 1) == 1){
    if(ch != '\n' && len < sizeof(buf) - 1){
      buf[len++] = ch;
      continue;
    }
    // 遇到 '\n'：处理完一整行
    buf[len] = '\0';
    len = 0;

    // 把 buf 按空格/制表符拆成参数，追加到 args[fixed..]
    int idx = fixed;
    char *p = buf;
    while(*p){
      while(*p == ' ' || *p == '\t')  // 跳过前导空白
        p++;
      if(*p == '\0')
        break;
      if(idx >= MAXARG - 1){          // 参数数量超限
        fprintf(2, "xargs: too many arguments\n");
        exit(1);
      }
      args[idx++] = p;                // 记下单词起点
      while(*p && *p != ' ' && *p != '\t')  // 走到单词末尾
        p++;
      if(*p)                          // 用 '\0' 切断，作为该参数的结尾
        *p++ = '\0';
    }

    // 该行确实拆出了参数才执行命令（空行跳过）
    if(idx > fixed){
      args[idx] = 0;
      if(fork() == 0){
        exec(args[0], args);
        fprintf(2, "xargs: exec %s failed\n", args[0]);
        exit(1);
      }
      wait(0);
    }
  }
  exit(0);
}
