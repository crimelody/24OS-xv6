#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // 把 mbuf 指向的以太网帧交给 e1000 DMA 发送：
  //   1) 取下一个可用的 TX 描述符位置（TDT）
  //   2) 若该描述符还没做完上次发送（无 DD 位）→ ring 满，返回 -1
  //   3) 若槽里还留着上一个 mbuf 的指针，先释放它
  //   4) 填描述符：addr=mbuf 数据、length、cmd=RS|EOP
  //   5) 把 mbuf 指针存入槽位（DMA 完成后再释放）
  //   6) 推进 TDT 通知网卡"有新包可发"
  //
  acquire(&e1000_lock);

  int idx = regs[E1000_TDT];              // 下一个要用的描述符
  struct tx_desc *desc = &tx_ring[idx];

  // DD 位未置 → 该槽还在被网卡使用（上一个包没发完），ring 满
  if((desc->status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1;
  }

  // 释放该槽上次使用的 mbuf（首次为 0，后续发完才轮到本槽）
  if(tx_mbufs[idx] != 0)
    mbuffree(tx_mbufs[idx]);

  // 装载新帧
  tx_mbufs[idx] = m;
  desc->addr = (uint64)m->head;            // 帧数据所在
  desc->length = m->len;                   // 帧长度
  desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; // 整包+要状态回报
  desc->status = 0;                        // 清旧状态

  // 推进 Tail，网卡即开始 DMA 发送
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;

  __sync_synchronize();                    // 确保写已对设备可见
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // 循环取走网卡收到的所有包：
  //   1) 从 RDT+1（下一个待处理位）开始
  //   2) 描述符 DD 位未置 → 没有新包，结束
  //   3) 用 mbufput 按网卡写入的长度修正 mbuf
  //   4) 交给协议栈 net_rx() 处理（会负责释放）
  //   5) 分配新 mbuf 还回 ring，让网卡能继续收
  //   6) 推进 RDT，循环处理同一时刻可能堆积的多个包
  //
  while(1){
    int idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;   // 下一个有数据的槽
    struct rx_desc *desc = &rx_ring[idx];

    if((desc->status & E1000_RXD_STAT_DD) == 0)
      break;                              // 无新包

    // 收到一个包：取 mbuf、修正其长度（网卡 DMA 了多少字节）
    struct mbuf *m = rx_mbufs[idx];
    mbufput(m, desc->length);

    // 交给网络协议栈处理（内部最终 mbuffree）
    net_rx(m);

    // 归还新缓冲给网卡，复位状态，推进 RDT
    rx_mbufs[idx] = mbufalloc(0);
    if(rx_mbufs[idx] == 0)
      panic("e1000");
    desc->addr = (uint64)rx_mbufs[idx]->head;
    desc->status = 0;
    regs[E1000_RDT] = idx;
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
