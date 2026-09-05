#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#ifdef LAB_LOCK
#define NBUCKET      13      // lab 8: bcache 哈希分桶数（质数利于分散）
#define FSSIZE       10000   // lab 8: usertests 的 writebig 等需更大文件系统
                             //（过小会 balloc: out of blocks）
#else
#define FSSIZE       1000  // size of file system in blocks
#endif
#define MAXPATH      128   // maximum file path name
