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
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  //使用锁来保证只有一个进程在用
  acquire(&e1000_lock);

  //首先拿到TX ring的索引
  uint32 idx = regs[E1000_TDT] ;
  struct tx_desc *tx = &tx_ring[idx];

  //检查是否溢出，查看是否还没完成上一次的请求，是则报错
  if(!(tx->status & E1000_TXD_STAT_DD)){
    release(&e1000_lock);
    return -1;
  }

  //如果没错就释放得到的mbuf内存
  if(tx_mbufs[idx]){
    mbuffree(tx_mbufs[idx]);
    tx_mbufs[idx] = 0;
  }
  //得到文件描述符，写入对应位置，需要设置flag（看看E1000），保存此次的buf用于之后释放
  /*cmd   
  IDE 延迟中断，减少中断频率 1/0 
  VLE VLAN标签，不知道作用，要CTL寄存器设置 1/0 
  DEXT 保留位，写0防止不兼容  0
  RPS 确保数据包发送完成再标记完成，提升可靠性 0/1
  RS 完成后更新 DD 状态 为 1，之后软件检测并回收描述符用
  IC 自动校验和，要配置 CSO 和 CCS字段 1/0
  IFCS 自动插入帧校验序列，以太网标准要求 1/0
  EOP 标记数据包结束，控制 IC，IFCS，VLE 有效 1 
  */ 
  tx->addr = (uint64) m->head;
  tx->length = m->len;
  tx->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

  //暂存m
  tx_mbufs[idx] = m;
  //更新ring的索引，取模
  regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;

  release(&e1000_lock);

  //如果成功得到一个空buff，就return 0，否则-1

  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  //E1000_RDT 读取寄存器，询问下一个等待接收的数据包ring索引，得到一个数据包？
  //while原因，一次中断可能要处理多个数据包，如果处理不完ring可能溢出，不再处理新的包，
  //新的包得不到响应，进程ping发送数据包得不到回应，退不出ping，父进程又等待子进程退出，死锁
  while(1){
    uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  
    //检查文件描述符是否有新数据包可用，否则停止？或许在这里处理包数量超16，等待有在往下做
    struct rx_desc *rx = &rx_ring[idx];

    if(!(rx->status & E1000_RXD_STAT_DD)){
      return; 
    }

    //更新mbf中长度为描述符中m-len长度，使用net_rx()传输mbuf到网络栈
    rx_mbufs[idx]->len = rx->length;
    net_rx(rx_mbufs[idx]);

    //使用alloc为mbuf分配空间，替换刚使用的，添加到descriptor中
    rx_mbufs[idx] = mbufalloc(0);
    rx->status = 0; // 初始化status
    rx->addr = (uint64) rx_mbufs[idx]->head;

    //更新E1000寄存器为处理过的最后一个环描述符索引，即当前索引
    regs[E1000_RDT] = idx;
  }
  //参考e1000_init初始化
  //另外需要处理数据包数量超过16的情况
    //或许检查是否当前要替换的包是有信息的  
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
