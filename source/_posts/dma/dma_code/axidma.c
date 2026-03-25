#ifdef ARCH_STAR
	#include "ARMv8MML_SP.h"
#elif ARCH_A55 && (!CONFIG_INTC_DIS)
	#include <intc.h>
#endif
#ifdef ARCH_A55
#include <system.h>
#include <platform.h>
#endif
#include <stdio.h>
#include <regs.h>
#include <stdint.h>
#include <timer.h>
#include <string.h>
#include <types.h>
#include "firewall.h"

#define SHOW_DDR_PERF 0
#if SHOW_DDR_PERF
#include <pm.h>
#include <crp.h>
#endif

#define AXIDMA1_BASE_ADDR	0xA7500000
#define AXIDMA2_BASE_ADDR	0xA7510000

#define AXIDMA1_IRQ_NUM		81
#define AXIDMA2_IRQ_NUM		82

/* Common registers offset */
#define DMAC_ID			0x000 /* R DMAC ID */
#define DMAC_COMPVER		0x008 /* R DMAC Component Version */
#define DMAC_CFG		0x010 /* R/W DMAC Configuration */
#define DMAC_CHEN		0x018 /* R/W DMAC Channel Enable */
#define DMAC_CHEN_L		0x018 /* R/W DMAC Channel Enable 00-31 */
#define DMAC_CHEN_H		0x01C /* R/W DMAC Channel Enable 32-63 */
#define DMAC_CHSUSP		0x020 /* R/w DMAC Channel Suspend Register*/
#define DMAC_CHABORT	0x028 /* R/w DMAC Channel Abort Register*/
#define DMAC_INTSTATUS		0x030 /* R DMAC Interrupt Status */
#define DMAC_COMMON_INTCLEAR	0x038 /* W DMAC Interrupt Clear */
#define DMAC_COMMON_INTSTATUS_ENA 0x040 /* R DMAC Interrupt Status Enable */
#define DMAC_COMMON_INTSIGNAL_ENA 0x048 /* R/W DMAC Interrupt Signal Enable */
#define DMAC_COMMON_INTSTATUS	0x050 /* R DMAC Interrupt Status */
#define DMAC_RESET		0x058 /* R DMAC Reset Register1 */
#define DMAC_LOWPOWER_CFG		0x060 /* R/W DMAC Low Power Configuration Register */
#define DMAC_COMMON_PARCTL		0x070 /* R/W DMAC Parity Control Register */
#define DMAC_COMMON_ECCCTLSTATUS	0x070 /* R/W DMAC  ECC Control and Status Register */


// 0x100 + (x -1) * 0x100
/* DMA channel registers offset */
#define CH_SAR_L		0x000 /* R/W Chan Source Address */
#define CH_SAR_H		0x004 /* R/W Chan Source Address */
#define CH_DAR_L		0x008 /* R/W Chan Destination Address */
#define CH_DAR_H		0x00C /* R/W Chan Destination Address */
#define CH_BLOCK_TS		0x010 /* R/W Chan Block Transfer Size */
#define CH_CTL			0x018 /* R/W Chan Control */
#define CH_CTL_L		0x018 /* R/W Chan Control 00-31 */
#define CH_CTL_H		0x01C /* R/W Chan Control 32-63 */
#define CH_CFG			0x020 /* R/W Chan Configuration */
#define CH_CFG_L		0x020 /* R/W Chan Configuration 00-31 */
#define CH_CFG_H		0x024 /* R/W Chan Configuration 32-63 */
#define CH_LLP			0x028 /* R/W Chan Linked List Pointer */
#define CH_LLP_L		0x028 /* R/W Chan Linked List Pointer */
#define CH_LLP_H		0x02C /* R/W Chan Linked List Pointer */
#define CH_STATUS		0x030 /* R Chan Status */
#define CH_SWHSSRC		0x038 /* R/W Chan SW Handshake Source */
#define CH_SWHSDST		0x040 /* R/W Chan SW Handshake Destination */
#define CH_BLK_TFR_RESUMEREQ	0x048 /* W Chan Block Transfer Resume Req */
#define CH_AXI_ID		0x050 /* R/W Chan AXI ID */
#define CH_AXI_QOS		0x058 /* R/W Chan AXI QOS */
#define CH_SSTAT		0x060 /* R Chan Source Status */
#define CH_DSTAT		0x068 /* R Chan Destination Status */
#define CH_SSTATAR		0x070 /* R/W Chan Source Status Fetch Addr */
#define CH_DSTATAR		0x078 /* R/W Chan Destination Status Fetch Addr */
#define CH_INTSTATUS_ENA	0x080 /* R/W Chan Interrupt Status Enable */
#define CH_INTSTATUS_ENA_H	0x084 /* R/W Chan Interrupt Status Enable */
#define CH_INTSTATUS		0x088 /* R/W Chan Interrupt Status */
#define CH_INTSTATUS_H		0x08C /* R/W Chan Interrupt Status */
#define CH_INTSIGNAL_ENA	0x090 /* R/W Chan Interrupt Signal Enable */
#define CH_INTSIGNAL_ENA_H	0x094 /* R/W Chan Interrupt Signal Enable */
#define CH_INTCLEAR		0x098 /* W Chan Interrupt Clear */

#define AXIDMA_COMPVER  0x3230302a

//#define axidma_reg_read(addr)               (*(volatile unsigned int  *) (addr))
//#define axidma_reg_write(addr,b)            ((*(volatile unsigned int  *) (addr)) = (b))

#define MAX_BLOCK_TRANSFER_SIZE 	0x40000
#define MAX_SG_NUMS 		300 // 20
#define MAX_DESC_NUMS 		300

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

enum
{
	BITS_8=0,
	BITS_16, 
	BITS_32,
	BITS_64,	
	BITS_128,
	BITS_256,
	BITS_512
};
	
struct axidma_desc
{
	unsigned int sar_l;
	unsigned int sar_h;
	unsigned int dar_l;
	unsigned int dar_h;
	unsigned int block_ts;
	unsigned int reserved;
	unsigned int llp_l;
	unsigned int llp_h;
	unsigned int ctrl_l;
	unsigned int ctrl_h;
	unsigned int sstat;
	unsigned int dstat;
	unsigned int llp_status_l;
	unsigned int llp_status_h;
	unsigned int reserved2;
	unsigned int reserved3;
};

struct axidma_config
{
	unsigned int id;
	unsigned int chann;
	
	uint64_t src_addr[MAX_SG_NUMS];
	uint64_t dst_addr[MAX_SG_NUMS];
	unsigned int len[MAX_SG_NUMS];
	unsigned int nums;
	
	unsigned int width;
	unsigned int prior;
	unsigned int intr_en;
};

static int g_interrupt_done=0;
int intc_register_done1=0;
int intc_register_done2=0;
static struct irq_handler axidma_handler1;
static struct irq_handler axidma_handler2;

static struct axidma_desc desc1[MAX_DESC_NUMS] __attribute__((aligned(64)));
static struct axidma_desc desc2[MAX_DESC_NUMS] __attribute__((aligned(64)));
static struct axidma_desc desc8[8][MAX_DESC_NUMS] __attribute__((aligned(64)));

static inline unsigned int axidma_reg_read(unsigned int reg)
{
    return read_mreg32((long)reg);
}

static inline void axidma_reg_write(unsigned int reg, unsigned int val)
{
    write_mreg32((long)reg, val);
}
	
	
void axidma_irq_handler1(unsigned int irq_id)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;

	//printf("%s %d\n",__func__, irq_id);
	base = AXIDMA1_BASE_ADDR + (axidma_handler1.irq_id -AXIDMA1_IRQ_NUM)* 0x10000;

	//intc_disable_normal_irq(axidma_handler1.irq_id);

	val = axidma_reg_read(base + DMAC_COMMON_INTSTATUS);
	axidma_reg_write(base + DMAC_COMMON_INTCLEAR, val);

	for(i=1;i<9;i++)
	{
		ch_base = base + 0x100 + (i-1) * 0x100;
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		axidma_reg_write(ch_base + CH_INTCLEAR, val);
	}
	
	g_interrupt_done = 1;
}

void axidma_irq_handler2(unsigned int irq_id)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	
	//printf("%s %d\n",__func__, irq_id);
	base = AXIDMA1_BASE_ADDR + (axidma_handler2.irq_id -AXIDMA1_IRQ_NUM)* 0x10000;
	
	//intc_disable_normal_irq(axidma_handler2.irq_id);
	
	val = axidma_reg_read(base + DMAC_COMMON_INTSTATUS);
	axidma_reg_write(base + DMAC_COMMON_INTCLEAR, val);
	
	for(i=1;i<9;i++)
	{
		ch_base = base + 0x100 + (i-1) * 0x100;
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		axidma_reg_write(ch_base + CH_INTCLEAR, val);
	}
	g_interrupt_done = 1;
}

int axidma_apb_init(unsigned int id)
{
	unsigned int val;

	// on clk
	val = axidma_reg_read(AP_CGM_EN_PERI0_ADDR);
	switch(id)
	{
		case 1:
			val |= AP_CGM_EN_CLK_AXI_DMA0_MASK;
			break;
		case 2:
			val |= AP_CGM_EN_CLK_AXI_DMA1_MASK;
			break;
		default:
			break;
	}
	axidma_reg_write(AP_CGM_EN_PERI0_ADDR, val);
	
	// off reset
	val = axidma_reg_read(AP_RST_SWRST_1_ADDR);
	switch(id)
	{
		case 1:
			val |= AP_RST_SWRST_AXI_DMA0_MASK;
			break;
		case 2:
			val |= AP_RST_SWRST_AXI_DMA1_MASK;
			break;
		default:
			break;
	}
	axidma_reg_write(AP_RST_SWRST_1_ADDR, val);

	usleep(1000);
	switch(id)
	{
		case 1: 
			val &= ~(AP_RST_SWRST_AXI_DMA0_MASK);
			break;
		case 2:
			val &= ~(AP_RST_SWRST_AXI_DMA1_MASK);
		default:
			break;
	}
	axidma_reg_write(AP_RST_SWRST_1_ADDR, val);
	return 0;
}

int dmac_init(unsigned int id)
{
	// clk and reset init
	return axidma_apb_init(id);
}

void dump_desc(struct axidma_desc *desc, unsigned int num)
{
	int i;
	
	for(i=0;i<num;i++)
	{
		printf("i=%d\n",i);
		printf("sar_l=0x%x\n",desc[i].sar_l);
		printf("sar_h=0x%x\n",desc[i].sar_h);
		printf("dar_l=0x%x\n",desc[i].dar_l);
		printf("dar_h=0x%x\n",desc[i].dar_h);
		printf("block_ts=0x%x\n",desc[i].block_ts);
		printf("llp_l=0x%x\n",desc[i].llp_l);
		printf("llp_h=0x%x\n",desc[i].llp_h);
		printf("ctrl_l=0x%x\n",desc[i].ctrl_l);
		printf("ctrl_h=0x%x\n",desc[i].ctrl_h);
		printf("sstat=0x%x\n",desc[i].sstat);
		printf("dstat=0x%x\n",desc[i].dstat);
		printf("llp_status_l=0x%x\n",desc[i].llp_status_l);
		printf("llp_status_h=0x%x\n",desc[i].llp_status_h);
		printf("\n");
	}
}

void __axidma_config(struct axidma_config config, struct axidma_desc *desc)
{
	int i,offset,n;
	unsigned int val;
	unsigned int base;
	unsigned int ch_base;
	unsigned int transfer_size;
	unsigned int transfer_val; 
	unsigned int transfer_width;
	unsigned int desc_sum=0;
	
	transfer_val = config.width;
	switch(transfer_val)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
	if(config.nums > MAX_SG_NUMS) {
		printf("invalid param, config.nums=%u > %d\n", config.nums, MAX_SG_NUMS);
	}

	for(i=0;i<config.nums;i++) {
		desc_sum += DIV_ROUND_UP(config.len[i], transfer_width/8 * MAX_BLOCK_TRANSFER_SIZE);
	}
	if(desc_sum > MAX_DESC_NUMS) {
		printf("invalid param, desc num=%d > %d\n", desc_sum, MAX_DESC_NUMS);
	}

	base = AXIDMA1_BASE_ADDR + (config.id -1)* 0x10000;
	ch_base = base + 0x100 + (config.chann -1) * 0x100;
	
	// channel configure
	val = axidma_reg_read(ch_base + CH_CFG_H);
	val &= ~(0x03); // DWAXIDMAC_TT_FC: MEM_TO_MEM_DMAC
	val |= config.prior << (49-32); // channel prioritys
	axidma_reg_write(ch_base + CH_CFG_H, val);
	
	val = axidma_reg_read(ch_base + CH_CFG_L);
	val |= 0x0f;  // LINKED_LIST
	axidma_reg_write(ch_base + CH_CFG_L, val);
	
	#if 0
	for(i=0;i<MAX_DESC_NUMS;i++)
	{
		printf("i=%d, %p\n", i, &desc[i]);
	}
	#endif
	
	i=0;
	for(n=0;n<config.nums;n++)
	{
		// channel descriptor
		offset=0;
		while(config.len[n])
		{
			transfer_size = config.len[n] > (transfer_width/8 * MAX_BLOCK_TRANSFER_SIZE) ? MAX_BLOCK_TRANSFER_SIZE : config.len[n] / (transfer_width/8);

			desc[i].sar_l = (config.src_addr[n] + offset) & 0xffffffff;
			desc[i].sar_h = ((config.src_addr[n] + offset) >> 32) & 0xffffffff;
			desc[i].dar_l = (config.dst_addr[n] + offset) & 0xffffffff;
			desc[i].dar_h = ((config.dst_addr[n] + offset) >> 32) & 0xffffffff;
			
			desc[i].block_ts = transfer_size-1;
			
			val = transfer_val << 11 | transfer_val << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
			desc[i].ctrl_l = val;
			
			val = axidma_reg_read(ch_base + CH_CTL_H);
			val |= (0x01U << 31);
			val &= ~(0x01 << 30);
			if(config.intr_en == 1) // IOC_BlkTfr	
				val |= 0x1 << 26; 
			else
				val &= ~(0x1 << 26);
			desc[i].ctrl_h = val;
			
			desc[i].llp_l = (uint32_t)(((uint64_t)(&desc[i+1])) & 0xffffffff);
			desc[i].llp_h = (uint32_t)((((uint64_t)(&desc[i+1])) >> 32) & 0xffffffff);
			
			i++;
			offset += transfer_size * transfer_width/8;
			config.len[n] -= transfer_size * transfer_width/8;
			//printf("transfer_width=%d,transfer_size=%d, config.len[%d]=0x%x\n",transfer_width,transfer_size,n,config.len[n]);
		}
		//printf("i=%d, n=%d, offset=%d\n",i,n,offset);
	}
	
	desc[i-1].ctrl_h |= 0x01 << 30; 
	
	axidma_reg_write(ch_base + CH_LLP_L, (uint32_t)(((uint64_t)&desc[0]) & 0xffffffff & (~0x3f)));
	axidma_reg_write(ch_base + CH_LLP_H, (uint32_t)((((uint64_t)&desc[0]) >> 32 )& 0xffffffff));
	
	#if 0
	dump_desc(desc, i);
	#endif
}	


void dmac_start(unsigned int id, unsigned int chann)
{
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;

	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
        ch_base = base + 0x100 + (chann -1) * 0x100;

	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
	
	val = axidma_reg_read(base + DMAC_CFG);
	val = val | (0x01 << 0); // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
}


int dmac_reg_rw_func01(unsigned int id)
{
	unsigned int val;
	unsigned int base;
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	axidma_reg_write(base+DMAC_CFG, 0x03);	
	val = axidma_reg_read(base + DMAC_ID);
	
	val = axidma_reg_read(base + DMAC_COMPVER);
	if(val == AXIDMA_COMPVER)
		return 0;
	else
		return -1;
}


int dmac_memcpy_func02(unsigned int id, unsigned int chann, uint64_t src_addr, uint64_t dst_addr, unsigned int len, unsigned int width)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	int error_flg=0;
	unsigned int transfer_width;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
	memset((unsigned char *)src_addr, 0x0, len * transfer_width/8);
	memset((unsigned char *)dst_addr, 0x0, len * transfer_width/8);
		
	#if 0	
	memset((unsigned char *)src_addr, 0x77, len);
	#else
	for(i=0;i<len * transfer_width/8;i++)
		*(unsigned char *)(src_addr+i) = i;
	#endif
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val & ~(0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=CONTINGUOUS
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	
	// 3. 源地址、目的地址、块传输大小、控制寄存器等配置 CHx_SAR/CHx_DAR/CHx_BLOCK_TS/CHx_CTL
	axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
	//printf("CH_SAR_L:0x%x = 0x%x in %s\n",ch_base + CH_SAR_L, src_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_SAR_H, src_addr >> 32);
	//printf("CH_SAR_H:0x%x = 0x%x in %s\n",ch_base + CH_SAR_H, src_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
	//printf("CH_DAR_L:0x%x = 0x%x in %s\n",ch_base + CH_DAR_L, dst_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_H, dst_addr >> 32);
	//printf("CH_DAR_H:0x%x = 0x%x in %s\n",ch_base + CH_DAR_H, dst_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_BLOCK_TS, len);
	//printf("CH_BLOCK_TS:0x%x = 0x%x in %s\n",ch_base + CH_BLOCK_TS, len, __func__);

	val = axidma_reg_read(ch_base + CH_CTL_L);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
		   
	val = width << 11 | width << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
	axidma_reg_write(ch_base + CH_CTL_L, val);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CTL_H);
	//printf("CH_CTL_H:0x%x = 0x%x in %s\n",ch_base + CH_CTL_H, val, __func__);
	val |= 0x1 << 26; // IOC_BlkTfr	   
	axidma_reg_write(ch_base + CH_CTL_H, val);

	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
		
	// 4. 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x ,actual=0x%x in %s\n",base + DMAC_CHEN_L, val, axidma_reg_read(base + DMAC_CHEN_L),__func__);
	
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS:0x%x=0x%x 0x%x in %s\n",ch_base + CH_INTSTATUS, val,__func__);
		if((val & 0x1) == 0x1)
		{
			axidma_reg_write(ch_base + CH_INTCLEAR, val);
			break;
		}
	}
	
	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x00);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x00);

	// 关闭通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val &= ~(0x1 << (chann-1));
	val &= ~(0x1 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 关闭dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val &= ~(0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	#if 1
	for(i=0;i<len * transfer_width/8;i++)
	{
		if(*(unsigned char *)(src_addr+i) != *(unsigned char *)(dst_addr+i))
		{
			error_flg++;
			printf("i=%d, src_addr=0x%x != dst_addr=0x%x\n",i, *(unsigned char *)(src_addr+i), *(unsigned char *)(dst_addr+i));
		}
	}
	#endif
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}

int dmac_memcpy_func02_timeout(unsigned int id, unsigned int chann, uint64_t src_addr, uint64_t dst_addr, unsigned int len, unsigned int width, int timeout)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	int error_flg=0;
	unsigned int transfer_width;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
	memset((unsigned char *)src_addr, 0x0, len * transfer_width/8);
	memset((unsigned char *)dst_addr, 0x0, len * transfer_width/8);
		
	#if 0	
	memset((unsigned char *)src_addr, 0x77, len);
	#else
	for(i=0;i<len * transfer_width/8;i++)
		*(unsigned char *)(src_addr+i) = i;
	#endif
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val & ~(0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=CONTINGUOUS
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	
	// 3. 源地址、目的地址、块传输大小、控制寄存器等配置 CHx_SAR/CHx_DAR/CHx_BLOCK_TS/CHx_CTL
	axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
	//printf("CH_SAR_L:0x%x = 0x%x in %s\n",ch_base + CH_SAR_L, src_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_SAR_H, src_addr >> 32);
	//printf("CH_SAR_H:0x%x = 0x%x in %s\n",ch_base + CH_SAR_H, src_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
	//printf("CH_DAR_L:0x%x = 0x%x in %s\n",ch_base + CH_DAR_L, dst_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_H, dst_addr >> 32);
	//printf("CH_DAR_H:0x%x = 0x%x in %s\n",ch_base + CH_DAR_H, dst_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_BLOCK_TS, len);
	//printf("CH_BLOCK_TS:0x%x = 0x%x in %s\n",ch_base + CH_BLOCK_TS, len, __func__);

	val = axidma_reg_read(ch_base + CH_CTL_L);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
		   
	val = width << 11 | width << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
	axidma_reg_write(ch_base + CH_CTL_L, val);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CTL_H);
	//printf("CH_CTL_H:0x%x = 0x%x in %s\n",ch_base + CH_CTL_H, val, __func__);
	val |= 0x1 << 26; // IOC_BlkTfr	   
	axidma_reg_write(ch_base + CH_CTL_H, val);

	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
		
	// 4. 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x ,actual=0x%x in %s\n",base + DMAC_CHEN_L, val, axidma_reg_read(base + DMAC_CHEN_L),__func__);
	
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	while(--timeout)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS:0x%x=0x%x 0x%x in %s\n",ch_base + CH_INTSTATUS, val,__func__);
		if((val & 0x1) == 0x1)
		{
			axidma_reg_write(ch_base + CH_INTCLEAR, val);
			break;
		}
		usleep(1000);
	}
	
	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x00);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x00);

	// 关闭通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val &= ~(0x1 << (chann-1));
	val &= ~(0x1 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 关闭dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val &= ~(0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	for(i=0;i<len * transfer_width/8;i++)
	{
		if(*(unsigned char *)(src_addr+i) != *(unsigned char *)(dst_addr+i))
		{
			error_flg++;
			printf("i=%d, src_addr=0x%x != dst_addr=0x%x\n",i, *(unsigned char *)(src_addr+i), *(unsigned char *)(dst_addr+i));
		}
	}
	
	if(timeout == 0 || error_flg)
		return -1;
	else
		return 0;
}


int dmac_memcpy_func02_perf(unsigned int id, unsigned int chann, uint64_t src_addr, uint64_t dst_addr, unsigned int len, unsigned int width)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	int error_flg=0;
	unsigned int transfer_width;
	
	unsigned long t1,t2,s1,ispeed;
	float speed;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
//	memset((unsigned char *)src_addr, 0x17, len * transfer_width/8);
//	memset((unsigned char *)dst_addr, 0x18, len * transfer_width/8);		
	#if 0
	memset((unsigned char *)src_addr, 0x77, len);
	#else
	for(i=0;i<0x100 * transfer_width/8;i++)
		*(unsigned char *)(src_addr+i) = i+1;
	#endif

	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	val |= 0xf << 23;
	val |= 0xf << 27;
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val & ~(0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=CONTINGUOUS
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	
	// 3. 源地址、目的地址、块传输大小、控制寄存器等配置 CHx_SAR/CHx_DAR/CHx_BLOCK_TS/CHx_CTL
	axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
	//printf("CH_SAR_L:0x%x = 0x%x in %s\n",ch_base + CH_SAR_L, src_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_SAR_H, src_addr >> 32);
	//printf("CH_SAR_H:0x%x = 0x%x in %s\n",ch_base + CH_SAR_H, src_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
	//printf("CH_DAR_L:0x%x = 0x%x in %s\n",ch_base + CH_DAR_L, dst_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_H, dst_addr >> 32);
	//printf("CH_DAR_H:0x%x = 0x%x in %s\n",ch_base + CH_DAR_H, dst_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_BLOCK_TS, len);
	//printf("CH_BLOCK_TS:0x%x = 0x%x in %s\n",ch_base + CH_BLOCK_TS, len, __func__);

	val = axidma_reg_read(ch_base + CH_CTL_L);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
		   
	val = width << 11 | width << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
	axidma_reg_write(ch_base + CH_CTL_L, val);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CTL_H);
	//printf("CH_CTL_H:0x%x = 0x%x in %s\n",ch_base + CH_CTL_H, val, __func__);
	val |= 0x1 << 26; // IOC_BlkTfr	   
	axidma_reg_write(ch_base + CH_CTL_H, val);

	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
	
#if SHOW_DDR_PERF
	pm_start_port(PORT4);
#endif
	t1 = get_timer_count();
	
	// 4. 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x ,actual=0x%x in %s\n",base + DMAC_CHEN_L, val, axidma_reg_read(base + DMAC_CHEN_L),__func__);
	
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
		if((val & 0x1) == 0x1)
		{
			axidma_reg_write(ch_base + CH_INTCLEAR, val);
			break;
		}
	}
	
	t2 = get_timer_count();
#if SHOW_DDR_PERF
	pm_end_port();
#endif
	s1 = (t1<t2)?(0xffffffff-t2+t1):(t1-t2);
	//speed = (transfer_width * len*1000)/(41.6667*s1);
	//speed = (24*(transfer_width/8) * len)/(s1);
	speed = (24000000ULL*(transfer_width/8)*len)/1024.0/1024.0/s1;
	ispeed = speed;
	printf("s1=%ld~~~~~~~~src=0x%llx, dst=0x%llx, width=%d,len=%d,t1=%ld, t2=%ld, speed=%ld\n",s1,src_addr, dst_addr, width, len, t1, t2, ispeed);
	
	
	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x00);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x00);

	// 关闭通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val &= ~(0x1 << (chann-1));
	val &= ~(0x1 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 关闭dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val &= ~(0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	#if 1
	for(i=0;i<0x10 * transfer_width/8;i++)
	{
		if(*(unsigned char *)(src_addr+i) != *(unsigned char *)(dst_addr+i))
		{
			error_flg++;
			printf("i=%d, src_addr=0x%x != dst_addr=0x%x\n",i, *(unsigned char *)(src_addr+i), *(unsigned char *)(dst_addr+i));
		}
	}
	#endif
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}


int dmac_memcpy_func02_max_perf(unsigned int id, uint64_t src_addr, uint64_t dst_addr, unsigned int len, unsigned int width)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	int error_flg=0;
	unsigned int transfer_width;
	
	unsigned long t1,t2,s1,ispeed;
	float speed;
	
	unsigned int chann;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
	//	memset((unsigned char *)src_addr, 0x17, len * transfer_width/8);
	//	memset((unsigned char *)dst_addr, 0x18, len * transfer_width/8);		
	#if 0
	memset((unsigned char *)src_addr, 0x77, len);
	#else
	for(i=0;i<0x100 * transfer_width/8;i++)
		*(unsigned char *)(src_addr+i) = i+1;
	#endif

	for(chann=1;chann<=8;chann++)
	{
		base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
		ch_base = base + 0x100 + (chann -1) * 0x100;
		
		// 0. 使能dmac
		val = axidma_reg_read(base + DMAC_CFG);
		//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
		val = val | (0x01 << 0) ; // DMAC_EN
		axidma_reg_write(base + DMAC_CFG, val);
		
		// 1. 获取一个空闲通道 DMAC_ChEn
		val = axidma_reg_read(base + DMAC_CHEN_L);
		//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
		
		// 2. 选择传输类型 CHx_CFG
		val = axidma_reg_read(ch_base + CH_CFG_H);
		//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
		val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
		val |= 0xf << 23;
		val |= 0xf << 27;
		axidma_reg_write(ch_base + CH_CFG_H, val);
		//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
		
		val = axidma_reg_read(ch_base + CH_CFG_L);
		//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
		val = val & ~(0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=CONTINGUOUS
		axidma_reg_write(ch_base + CH_CFG_L, val);
		//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
		
		// 3. 源地址、目的地址、块传输大小、控制寄存器等配置 CHx_SAR/CHx_DAR/CHx_BLOCK_TS/CHx_CTL
		axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
		//printf("CH_SAR_L:0x%x = 0x%x in %s\n",ch_base + CH_SAR_L, src_addr & 0xffffffff, __func__);	
		
		axidma_reg_write(ch_base + CH_SAR_H, src_addr >> 32);
		//printf("CH_SAR_H:0x%x = 0x%x in %s\n",ch_base + CH_SAR_H, src_addr >> 32, __func__);	
		
		axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
		//printf("CH_DAR_L:0x%x = 0x%x in %s\n",ch_base + CH_DAR_L, dst_addr & 0xffffffff, __func__);	
		
		axidma_reg_write(ch_base + CH_DAR_H, dst_addr >> 32);
		//printf("CH_DAR_H:0x%x = 0x%x in %s\n",ch_base + CH_DAR_H, dst_addr >> 32, __func__);	
		
		axidma_reg_write(ch_base + CH_BLOCK_TS, len);
		//printf("CH_BLOCK_TS:0x%x = 0x%x in %s\n",ch_base + CH_BLOCK_TS, len, __func__);

		val = axidma_reg_read(ch_base + CH_CTL_L);
		//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
			   
		val = width << 11 | width << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
		axidma_reg_write(ch_base + CH_CTL_L, val);
		//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
		
		val = axidma_reg_read(ch_base + CH_CTL_H);
		//printf("CH_CTL_H:0x%x = 0x%x in %s\n",ch_base + CH_CTL_H, val, __func__);
		val |= 0x1 << 26; // IOC_BlkTfr	   
		axidma_reg_write(ch_base + CH_CTL_H, val);
	}

#if SHOW_DDR_PERF
	pm_start_port(PORT4);
#endif
	t1 = get_timer_count();
	
	for(chann=1;chann<=8;chann++)
	{
		base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
		ch_base = base + 0x100 + (chann -1) * 0x100;
		
		// 使能通道的block和dma完成中断
		axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
		axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
		
		// 4. 使能通道 DMAC_ChEn
		val = axidma_reg_read(base + DMAC_CHEN_L);
		val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
		axidma_reg_write(base + DMAC_CHEN_L, val);
		//printf("DMAC_CHEN_L:0x%x = 0x%x ,actual=0x%x in %s\n",base + DMAC_CHEN_L, val, axidma_reg_read(base + DMAC_CHEN_L),__func__);
	}
	
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	
	for(chann=1;chann<=8;chann++)
	{
		base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
		ch_base = base + 0x100 + (chann -1) * 0x100;
		
		while(1)
		{
			val = axidma_reg_read(ch_base + CH_INTSTATUS);
			//printf("CH_INTSTATUS:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
			if((val & 0x1) == 0x1)
			{
				axidma_reg_write(ch_base + CH_INTCLEAR, val);
				break;
			}
		}
	}
	
	
	t2 = get_timer_count();
#if SHOW_DDR_PERF
	pm_end_port();
#endif
	s1 = (t1<t2)?(0xffffffff-t2+t1):(t1-t2);
	//speed = (transfer_width * len*1000)/(41.6667*s1);
	//speed = (24*(transfer_width/8) * len*8)/(s1);
	speed = (24000000ULL*(transfer_width/8)*len*8)/1024.0/1024.0/s1;
	ispeed = speed;
	printf("s1=%ld~~~~~~~~src=0x%llx, dst=0x%llx, width=%d,len=%d,t1=%ld, t2=%ld, speed=%ld\n",s1,src_addr, dst_addr, width, len, t1, t2, ispeed);
	
	
	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x00);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x00);

	// 关闭通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val &= ~(0x1 << (chann-1));
	val &= ~(0x1 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 关闭dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val &= ~(0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	#if 0
	for(i=0;i<0x10 * transfer_width/8;i++)
	{
		if(*(unsigned char *)(src_addr+i) != *(unsigned char *)(dst_addr+i))
		{
			error_flg++;
			printf("i=%d, src_addr=0x%x != dst_addr=0x%x\n",i, *(unsigned char *)(src_addr+i), *(unsigned char *)(dst_addr+i));
		}
	}
	#endif
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}


int dmac_memcpy_func02_two_axidma_max_perf( uint64_t src_addr, uint64_t dst_addr, unsigned int len, unsigned int width)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	int error_flg=0;
	unsigned int transfer_width;
	
	unsigned long t1,t2,s1,ispeed;
	float speed;
	
	unsigned int id;
	unsigned int chann;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
	//	memset((unsigned char *)src_addr, 0x17, len * transfer_width/8);
	//	memset((unsigned char *)dst_addr, 0x18, len * transfer_width/8);		
	#if 0
	memset((unsigned char *)src_addr, 0x77, len);
	#else
	for(i=0;i<0x100 * transfer_width/8;i++)
		*(unsigned char *)(src_addr+i) = i+1;
	#endif

	for(chann=1;chann<=8;chann++)
	{
		for(id=1;id<3;id++)
		{
			base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
			ch_base = base + 0x100 + (chann -1) * 0x100;
			
			// 0. 使能dmac
			val = axidma_reg_read(base + DMAC_CFG);
			//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
			val = val | (0x01 << 0) ; // DMAC_EN
			axidma_reg_write(base + DMAC_CFG, val);
			
			// 1. 获取一个空闲通道 DMAC_ChEn
			val = axidma_reg_read(base + DMAC_CHEN_L);
			//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
			
			// 2. 选择传输类型 CHx_CFG
			val = axidma_reg_read(ch_base + CH_CFG_H);
			//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
			val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
			val |= 0xf << 23;
			val |= 0xf << 27;
			axidma_reg_write(ch_base + CH_CFG_H, val);
			//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
			
			val = axidma_reg_read(ch_base + CH_CFG_L);
			//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
			val = val & ~(0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=CONTINGUOUS
			axidma_reg_write(ch_base + CH_CFG_L, val);
			//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
			
			// 3. 源地址、目的地址、块传输大小、控制寄存器等配置 CHx_SAR/CHx_DAR/CHx_BLOCK_TS/CHx_CTL
			axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
			//printf("CH_SAR_L:0x%x = 0x%x in %s\n",ch_base + CH_SAR_L, src_addr & 0xffffffff, __func__);	
			
			axidma_reg_write(ch_base + CH_SAR_H, src_addr >> 32);
			//printf("CH_SAR_H:0x%x = 0x%x in %s\n",ch_base + CH_SAR_H, src_addr >> 32, __func__);	
			
			axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
			//printf("CH_DAR_L:0x%x = 0x%x in %s\n",ch_base + CH_DAR_L, dst_addr & 0xffffffff, __func__);	
			
			axidma_reg_write(ch_base + CH_DAR_H, dst_addr >> 32);
			//printf("CH_DAR_H:0x%x = 0x%x in %s\n",ch_base + CH_DAR_H, dst_addr >> 32, __func__);	
			
			axidma_reg_write(ch_base + CH_BLOCK_TS, len);
			//printf("CH_BLOCK_TS:0x%x = 0x%x in %s\n",ch_base + CH_BLOCK_TS, len, __func__);

			val = axidma_reg_read(ch_base + CH_CTL_L);
			//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
				   
			val = width << 11 | width << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
			axidma_reg_write(ch_base + CH_CTL_L, val);
			//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
			
			val = axidma_reg_read(ch_base + CH_CTL_H);
			//printf("CH_CTL_H:0x%x = 0x%x in %s\n",ch_base + CH_CTL_H, val, __func__);
			val |= 0x1 << 26; // IOC_BlkTfr	   
			axidma_reg_write(ch_base + CH_CTL_H, val);
		}
	}

#if SHOW_DDR_PERF
	pm_start_port(PORT4);
#endif
	t1 = get_timer_count();
	
	for(chann=1;chann<=8;chann++)
	{
		for(id=1;id<3;id++)
		{
			base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
			ch_base = base + 0x100 + (chann -1) * 0x100;
			
			// 使能通道的block和dma完成中断
			axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
			axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
			
			// 4. 使能通道 DMAC_ChEn
			val = axidma_reg_read(base + DMAC_CHEN_L);
			val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
			axidma_reg_write(base + DMAC_CHEN_L, val);
			//printf("DMAC_CHEN_L:0x%x = 0x%x ,actual=0x%x in %s\n",base + DMAC_CHEN_L, val, axidma_reg_read(base + DMAC_CHEN_L),__func__);
		}
	}
	
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	
	for(chann=1;chann<=8;chann++)
	{
		for(id=1;id<3;id++)
		{
			base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
			ch_base = base + 0x100 + (chann -1) * 0x100;
			
			while(1)
			{
				val = axidma_reg_read(ch_base + CH_INTSTATUS);
				//printf("CH_INTSTATUS:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
				if((val & 0x1) == 0x1)
				{
					axidma_reg_write(ch_base + CH_INTCLEAR, val);
					break;
				}
			}
		}
	}
	
	t2 = get_timer_count();
#if SHOW_DDR_PERF
	pm_end_port();
#endif
	s1 = (t1<t2)?(0xffffffff-t2+t1):(t1-t2);
	//speed = (transfer_width * len*1000)/(41.6667*s1);
	//speed = (24*(transfer_width/8) * len*8*2)/(s1);
	speed = (24000000ULL*(transfer_width/8)*len*8*2)/1024.0/1024.0/s1;
	ispeed = speed;
	printf("s1=%ld~~~~~~~~src=0x%llx, dst=0x%llx, width=%d,len=%d,t1=%ld, t2=%ld, speed=%ld\n",s1,src_addr, dst_addr, width, len, t1, t2, ispeed);
	

	for(chann=1;chann<=8;chann++)
	{
		for(id=1;id<3;id++)
		{
			base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
			ch_base = base + 0x100 + (chann -1) * 0x100;
			
			// 使能通道的block和dma完成中断
			axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x00);
			axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x00);

			// 关闭通道 DMAC_ChEn
			val = axidma_reg_read(base + DMAC_CHEN_L);
			val &= ~(0x1 << (chann-1));
			val &= ~(0x1 << (chann-1+8));
			axidma_reg_write(base + DMAC_CHEN_L, val);
			// printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
			
			// 关闭dmac
			val = axidma_reg_read(base + DMAC_CFG);
			// printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
			val &= ~(0x01 << 0) ; // DMAC_EN
			axidma_reg_write(base + DMAC_CFG, val);
		}
	}
	
	#if 0
	for(i=0;i<0x10 * transfer_width/8;i++)
	{
		if(*(unsigned char *)(src_addr+i) != *(unsigned char *)(dst_addr+i))
		{
			error_flg++;
			printf("i=%d, src_addr=0x%x != dst_addr=0x%x\n",i, *(unsigned char *)(src_addr+i), *(unsigned char *)(dst_addr+i));
		}
	}
	#endif
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}

int dmac_interrupt_func03(unsigned int id, unsigned int chann, uint64_t src_addr, uint64_t dst_addr, unsigned int len, unsigned int width)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	int error_flg=0;
	unsigned int transfer_width;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	
	memset((unsigned char *)src_addr, 0x0, len * transfer_width/8);
	memset((unsigned char *)dst_addr, 0x0, len * transfer_width/8);
		
	#if 0	
	memset((unsigned char *)src_addr, 0x77, len);
	#else
	for(i=0;i<len * transfer_width/8;i++)
		*(unsigned char *)(src_addr+i) = i;
	#endif
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val & ~(0x01 << 1) ; // INT_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 5.关闭通道的所有中断
	for(i=1;i<9;i++)
	{
		axidma_reg_write(base + 0x100 + (i -1) * 0x100 + CH_INTSTATUS_ENA, 0x0);
		axidma_reg_write(base + 0x100 + (i -1) * 0x100 + CH_INTSIGNAL_ENA, 0x0);
	}
	//axidma_reg_write(base + DMAC_COMMON_INTSTATUS_ENA, 0x0);
	//axidma_reg_write(base + DMAC_COMMON_INTSIGNAL_ENA, 0x0);
	
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val & ~(0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=CONTINGUOUS
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	
	// 3. 源地址、目的地址、块传输大小、控制寄存器等配置 CHx_SAR/CHx_DAR/CHx_BLOCK_TS/CHx_CTL
	axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
	//printf("CH_SAR_L:0x%x = 0x%x in %s\n",ch_base + CH_SAR_L, src_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_SAR_H, src_addr >> 32);
	//printf("CH_SAR_H:0x%x = 0x%x in %s\n",ch_base + CH_SAR_H, src_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
	//printf("CH_DAR_L:0x%x = 0x%x in %s\n",ch_base + CH_DAR_L, dst_addr & 0xffffffff, __func__);	
	
	axidma_reg_write(ch_base + CH_DAR_H, dst_addr >> 32);
	//printf("CH_DAR_H:0x%x = 0x%x in %s\n",ch_base + CH_DAR_H, dst_addr >> 32, __func__);	
	
	axidma_reg_write(ch_base + CH_BLOCK_TS, len);
	//printf("CH_BLOCK_TS:0x%x = 0x%x in %s\n",ch_base + CH_BLOCK_TS, len, __func__);

	val = axidma_reg_read(ch_base + CH_CTL_L);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
	
//	val = 0x04 << 11 | 0x04 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
	val = width << 11 | width << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;		   
	axidma_reg_write(ch_base + CH_CTL_L, val);
	//printf("CH_CTL_L:0x%x = 0x%x in %s\n",ch_base + CH_CTL_L, val, __func__);
	
	val = axidma_reg_read(ch_base + CH_CTL_H);
	//printf("CH_CTL_H:0x%x = 0x%x in %s\n",ch_base + CH_CTL_H, val, __func__);
	
	// 4. 注册中断
	if((id == 1) && (intc_register_done1 != 1))
	{
		intc_register_done1 = 1;
		axidma_handler1.dev_name = "Axidma1";
		axidma_handler1.irq_id = AXIDMA1_IRQ_NUM + (id -1);
		axidma_handler1.priority = 0;
		axidma_handler1.handler = axidma_irq_handler1;
		axidma_handler1.b_fast_irq = 0;
		axidma_handler1.next = NULL;
		intc_request_irq(&axidma_handler1);
		intc_set_target(0x1, axidma_handler1.irq_id);
		intc_enable_normal_irq(axidma_handler1.irq_id);
	}
	
	if((id == 2) && (intc_register_done2 != 1))
	{
		intc_register_done2 = 1;
		axidma_handler2.dev_name = "Axidma2";
		axidma_handler2.irq_id = AXIDMA1_IRQ_NUM + (id -1);
		axidma_handler2.priority = 0;
		axidma_handler2.handler = axidma_irq_handler2;
		axidma_handler2.b_fast_irq = 0;
		axidma_handler2.next = NULL;
		intc_request_irq(&axidma_handler2);
		intc_set_target(0x1, axidma_handler2.irq_id);
		intc_enable_normal_irq(axidma_handler2.irq_id);
	}
	
	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);
		
	// 使能中断
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 1) ; // INT_EN
	axidma_reg_write(base + DMAC_CFG, val);

	// 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (0x1 << (chann-1) | 0x01 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	while(1)
	{
		if(g_interrupt_done == 1)
		{
			g_interrupt_done = 0;
			break;
		}
	}
	
	#if 0
	// 关闭通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val &= ~(0x1 << (chann-1));
	val &= ~(0x1 << (chann-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	#else
	axidma_reg_write(base + DMAC_CHEN_L, 0x0);
	#endif
	
	// 关闭通道的所有中断
	for(i=1;i<9;i++)
	{
		axidma_reg_write(base + 0x100 + (i -1) * 0x100 + CH_INTSTATUS_ENA, 0x0);
		axidma_reg_write(base + 0x100 + (i -1) * 0x100 + CH_INTSIGNAL_ENA, 0x0);
	}
	//axidma_reg_write(base + DMAC_COMMON_INTSTATUS_ENA, 0x0);
	//axidma_reg_write(base + DMAC_COMMON_INTSIGNAL_ENA, 0x0);

	#if 0	
	// 关闭dmac/关闭中断
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val &= ~(0x01 << 0) ; // DMAC_EN
	val &= ~(0x01 << 1) ; // INT_EN
	axidma_reg_write(base + DMAC_CFG, val);
	#else
	axidma_reg_write(base + DMAC_CFG, 0x0);
	#endif
	
	for(i=0;i<len * transfer_width/8;i++)
	{
		if(*(unsigned char *)(src_addr+i) != *(unsigned char *)(dst_addr+i))
		{
			error_flg++;
			printf("i=%d, src_addr=0x%x != dst_addr=0x%x\n",i, *(unsigned char *)(src_addr+i), *(unsigned char *)(dst_addr+i));
		}
	}
			
	if(error_flg == 0)
		return 0;
	else
		return -1;
}

#if 0
void dmac_memcpy_lli_func04(unsigned int id, unsigned int chann, uint64_t src_addr1, uint64_t dst_addr1, unsigned int len1, 
					uint64_t src_addr2, uint64_t dst_addr2, unsigned int len2)
{
	int i;
	int error_flg=0;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	struct axidma_desc *desc1=0x200020000;
	struct axidma_desc *desc2=0x200030000;

	#if 0
		memset((unsigned char *)src_addr1, 0x77, len1);
		memset((unsigned char *)src_addr2, 0x88, len2);
	#else
		for(i=0;i<len1;i++)
		{
			*(unsigned char *)(src_addr1+i) = i;
		}
		
		for(i=0;i<len2;i++)
		{
			*(unsigned char *)(src_addr2+i) = i;
		}
	#endif
	
	memset((unsigned char *)dst_addr1, 0x0, len2);
	memset((unsigned char *)dst_addr2, 0x0, len2);
	memset((struct axidma_descr *)desc1, 0x0, sizeof(struct axidma_desc));
	memset((struct axidma_descr *)desc2, 0x0, sizeof(struct axidma_desc));
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);

	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val | (0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=LINKED_LIST
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);


	// 3. 生成2个描述符
	desc1->sar_l = src_addr1 & 0xffffffff;
	desc1->sar_h = src_addr1 >> 32;
	desc1->dar_l = dst_addr1 & 0xffffffff;
	desc1->dar_h = dst_addr1 >> 32;
	desc1->block_ts = len1;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc1->ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x00 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc1->ctrl_h = val;
	desc1->llp_l = ((uint64_t)desc2) & 0xffffffff & (~0x3f);
	desc1->llp_h = ((uint64_t)desc2 >> 32) & 0xffffffff;
	
	desc2->sar_l = src_addr2 & 0xffffffff;
	desc2->sar_h = src_addr2 >> 32;
	desc2->dar_l = dst_addr2 & 0xffffffff;
	desc2->dar_h = dst_addr2 >> 32;
	desc2->block_ts = len2;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc2->ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x01 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc2->ctrl_h = val;
	
	//printf("%p %p\n",(uint64_t)desc1, (uint64_t)desc2);
	//axidma_reg_write(ch_base + CH_LLP_L, ((uint64_t)0x200020000) & 0xffffffff & (~0x3f));
	//axidma_reg_write(ch_base + CH_LLP_L, 0x20000);
	axidma_reg_write(ch_base + CH_LLP_L, ((uint64_t)desc1) & 0xffffffff & (~0x3f));
	//axidma_reg_write(ch_base + CH_LLP_H, (((uint64_t)0x200020000) >> 32 )& 0xffffffff);
	//axidma_reg_write(ch_base + CH_LLP_H, 0x02);
	axidma_reg_write(ch_base + CH_LLP_H, (((uint64_t)desc1) >> 32 )& 0xffffffff);

	printf("sar_l=0x%x\n",desc1->sar_l);
	printf("sar_h=0x%x\n",desc1->sar_h);
	printf("dar_l=0x%x\n",desc1->dar_l);
	printf("dar_h=0x%x\n",desc1->dar_h);
	printf("block_ts=0x%x\n",desc1->block_ts);
	printf("llp_l=0x%x\n",desc1->llp_l);
	printf("llp_h=0x%x\n",desc1->llp_h);
	printf("ctrl_l=0x%x\n",desc1->ctrl_l);
	printf("ctrl_h=0x%x\n",desc1->ctrl_h);
	printf("sstat=0x%x\n",desc1->sstat);
	printf("dstat=0x%x\n",desc1->dstat);
	printf("llp_status_l=0x%x\n",desc1->llp_status_l);
	printf("llp_status_h=0x%x\n",desc1->llp_status_h);
	
	// 4. 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (chann << 0 | chann << 8);
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	usleep(1000);
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		printf("CH_INTSTATUS4:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
		if((val & 0x3) == 0x3)
		{
			break;
		}
	}
	
	for(i=0;i<len1;i++)
	{
		if(*(unsigned char *)(src_addr1+i) != *(unsigned char *)(dst_addr1+i))
		{
			error_flg++;
			printf("i=%d, src_addr1=0x%x != dst_addr1=0x%x\n",i, *(unsigned char *)(src_addr1+i), *(unsigned char *)(dst_addr1+i));
		}
	}
	
	for(i=0;i<len2;i++)
	{
		if(*(unsigned char *)(src_addr2+i) != *(unsigned char *)(dst_addr2+i))
		{
			error_flg++;
			printf("i=%d, src_addr2=0x%x != dst_addr2=0x%x\n",i, *(unsigned char *)(src_addr2+i), *(unsigned char *)(dst_addr2+i));
		}
	}
	
	if(error_flg == 0)
		printf("%s ------------------ pass\n",__func__);
	else
		printf("%s ------------------ fail\n",__func__);
}
#endif

void dmac_memcpy_lli_func04_2(unsigned int id, unsigned int chann, uint64_t src_addr1, uint64_t dst_addr1, unsigned int len1, 
					uint64_t src_addr2, uint64_t dst_addr2, unsigned int len2)
{
	int i;
	int error_flg=0;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	struct axidma_desc desc1 __attribute__((aligned(64)));
	struct axidma_desc desc2 __attribute__((aligned(64)));

	#if 0
		memset((unsigned char *)src_addr1, 0x77, len1);
		memset((unsigned char *)src_addr2, 0x88, len2);
	#else
		for(i=0;i<len1;i++)
		{
			*(unsigned char *)(src_addr1+i) = i;
		}
		
		for(i=0;i<len2;i++)
		{
			*(unsigned char *)(src_addr2+i) = i;
		}
	#endif
	
	memset((unsigned char *)dst_addr1, 0x0, len2);
	memset((unsigned char *)dst_addr2, 0x0, len2);
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);

	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val | (0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=LINKED_LIST
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);


	// 3. 生成2个描述符
	desc1.sar_l = src_addr1 & 0xffffffff;
	desc1.sar_h = src_addr1 >> 32;
	desc1.dar_l = dst_addr1 & 0xffffffff;
	desc1.dar_h = dst_addr1 >> 32;
	desc1.block_ts = len1;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc1.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x00 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc1.ctrl_h = val;
	desc1.llp_l = ((uint64_t)&desc2) & 0xffffffff & (~0x3f);
	desc1.llp_h = ((uint64_t)&desc2 >> 32) & 0xffffffff;
	
	desc2.sar_l = src_addr2 & 0xffffffff;
	desc2.sar_h = src_addr2 >> 32;
	desc2.dar_l = dst_addr2 & 0xffffffff;
	desc2.dar_h = dst_addr2 >> 32;
	desc2.block_ts = len2;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc2.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x01 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc2.ctrl_h = val;
	
	//printf("%p %p\n",(uint64_t)&desc1, (uint64_t)&desc2);
	//axidma_reg_write(ch_base + CH_LLP_L, ((uint64_t)0x200020000) & 0xffffffff & (~0x3f));
	//axidma_reg_write(ch_base + CH_LLP_L, 0x20000);
	axidma_reg_write(ch_base + CH_LLP_L, ((uint64_t)&desc1) & 0xffffffff & (~0x3f));
	//axidma_reg_write(ch_base + CH_LLP_H, (((uint64_t)0x200020000) >> 32 )& 0xffffffff);
	//axidma_reg_write(ch_base + CH_LLP_H, 0x02);
	axidma_reg_write(ch_base + CH_LLP_H, (((uint64_t)&desc1) >> 32 )& 0xffffffff);

	printf("sar_l=0x%x\n",desc1.sar_l);
	printf("sar_h=0x%x\n",desc1.sar_h);
	printf("dar_l=0x%x\n",desc1.dar_l);
	printf("dar_h=0x%x\n",desc1.dar_h);
	printf("block_ts=0x%x\n",desc1.block_ts);
	printf("llp_l=0x%x\n",desc1.llp_l);
	printf("llp_h=0x%x\n",desc1.llp_h);
	printf("ctrl_l=0x%x\n",desc1.ctrl_l);
	printf("ctrl_h=0x%x\n",desc1.ctrl_h);
	printf("sstat=0x%x\n",desc1.sstat);
	printf("dstat=0x%x\n",desc1.dstat);
	printf("llp_status_l=0x%x\n",desc1.llp_status_l);
	printf("llp_status_h=0x%x\n",desc1.llp_status_h);
	
	// 4. 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (chann << 0 | chann << 8);
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	usleep(1000);
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS4:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
		if((val & 0x3) == 0x3)
		{
			break;
		}
	}
	
	for(i=0;i<len1;i++)
	{
		if(*(unsigned char *)(src_addr1+i) != *(unsigned char *)(dst_addr1+i))
		{
			error_flg++;
			printf("i=%d, src_addr1=0x%x != dst_addr1=0x%x\n",i, *(unsigned char *)(src_addr1+i), *(unsigned char *)(dst_addr1+i));
		}
	}
	
	for(i=0;i<len2;i++)
	{
		if(*(unsigned char *)(src_addr2+i) != *(unsigned char *)(dst_addr2+i))
		{
			error_flg++;
			printf("i=%d, src_addr2=0x%x != dst_addr2=0x%x\n",i, *(unsigned char *)(src_addr2+i), *(unsigned char *)(dst_addr2+i));
		}
	}
	
	if(error_flg == 0)
		printf("%s ------------------ pass\n",__func__);
	else
		printf("%s ------------------ fail\n",__func__);
}


void dmac_memcpy_lli_func04_3(unsigned int id, unsigned int chann, uint64_t src_addr1, uint64_t dst_addr1, unsigned int len1, 
					uint64_t src_addr2, uint64_t dst_addr2, unsigned int len2,
					uint64_t src_addr3, uint64_t dst_addr3, unsigned int len3,
					uint64_t src_addr4, uint64_t dst_addr4, unsigned int len4,
					uint64_t src_addr5, uint64_t dst_addr5, unsigned int len5)
{
	int i;
	int error_flg=0;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;
	struct axidma_desc desc1 __attribute__((aligned(64)));
	struct axidma_desc desc2 __attribute__((aligned(64)));
	struct axidma_desc desc3 __attribute__((aligned(64)));
	struct axidma_desc desc4 __attribute__((aligned(64)));
	struct axidma_desc desc5 __attribute__((aligned(64)));

	#if 0
		memset((unsigned char *)src_addr1, 0x77, len1);
		memset((unsigned char *)src_addr2, 0x88, len2);
		memset((unsigned char *)src_addr3, 0x99, len3);
	#else
		for(i=0;i<len1;i++)
		{
			*(unsigned char *)(src_addr1+i) = i;
		}
		
		for(i=0;i<len2;i++)
		{
			*(unsigned char *)(src_addr2+i) = i;
		}
		for(i=0;i<len3;i++)
		{
			*(unsigned char *)(src_addr3+i) = i;
		}
		for(i=0;i<len4;i++)
		{
			*(unsigned char *)(src_addr4+i) = i;
		}
		for(i=0;i<len5;i++)
		{
			*(unsigned char *)(src_addr5+i) = i;
		}
	#endif
	
	memset((unsigned char *)dst_addr1, 0x0, len2);
	memset((unsigned char *)dst_addr2, 0x0, len2);
	memset((unsigned char *)dst_addr3, 0x0, len3);
	memset((unsigned char *)dst_addr4, 0x0, len4);
	memset((unsigned char *)dst_addr5, 0x0, len5);
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base = base + 0x100 + (chann -1) * 0x100;
	
	// 0. 使能dmac
	val = axidma_reg_read(base + DMAC_CFG);
	//printf("DMAC_CFG:0x%x = 0x%x in %s\n",base + DMAC_CFG, val, __func__);
	val = val | (0x01 << 0) ; // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	// 1. 获取一个空闲通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	// 2. 选择传输类型 CHx_CFG
	val = axidma_reg_read(ch_base + CH_CFG_H);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);
	val = val & ~(0x03); // DWAXIDMAC_TT_FC_MEM_TO_MEM_DMAC
	axidma_reg_write(ch_base + CH_CFG_H, val);
	//printf("CH_CFG_H:0x%x = 0x%x in %s\n",ch_base + CH_CFG_H, val, __func__);

	val = axidma_reg_read(ch_base + CH_CFG_L);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);
	val = val | (0x0f); // DST_MULTBLK_TYPE=SRC_MULTBLK_TYPE=LINKED_LIST
	axidma_reg_write(ch_base + CH_CFG_L, val);
	//printf("CH_CFG_L:0x%x = 0x%x in %s\n",ch_base + CH_CFG_L, val, __func__);


	// 3. 生成2个描述符
	desc1.sar_l = src_addr1 & 0xffffffff;
	desc1.sar_h = src_addr1 >> 32;
	desc1.dar_l = dst_addr1 & 0xffffffff;
	desc1.dar_h = dst_addr1 >> 32;
	desc1.block_ts = len1;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc1.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x00 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc1.ctrl_h = val;
	desc1.llp_l = ((uint64_t)&desc2) & 0xffffffff & (~0x3f);
	desc1.llp_h = ((uint64_t)&desc2 >> 32) & 0xffffffff;
	
	desc2.sar_l = src_addr2 & 0xffffffff;
	desc2.sar_h = src_addr2 >> 32;
	desc2.dar_l = dst_addr2 & 0xffffffff;
	desc2.dar_h = dst_addr2 >> 32;
	desc2.block_ts = len2;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc2.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x00 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc2.ctrl_h = val;
	desc2.llp_l = ((uint64_t)&desc3) & 0xffffffff & (~0x3f);
	desc2.llp_h = ((uint64_t)&desc3 >> 32) & 0xffffffff;
	
	
	desc3.sar_l = src_addr3 & 0xffffffff;
	desc3.sar_h = src_addr3 >> 32;
	desc3.dar_l = dst_addr3 & 0xffffffff;
	desc3.dar_h = dst_addr3 >> 32;
	desc3.block_ts = len3;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc3.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x00 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc3.ctrl_h = val;
	desc3.llp_l = ((uint64_t)&desc4) & 0xffffffff & (~0x3f);
	desc3.llp_h = ((uint64_t)&desc4 >> 32) & 0xffffffff;
	
	desc4.sar_l = src_addr4 & 0xffffffff;
	desc4.sar_h = src_addr4 >> 32;
	desc4.dar_l = dst_addr4 & 0xffffffff;
	desc4.dar_h = dst_addr4 >> 32;
	desc4.block_ts = len4;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc4.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x00 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc4.ctrl_h = val;
	desc4.llp_l = ((uint64_t)&desc5) & 0xffffffff & (~0x3f);
	desc4.llp_h = ((uint64_t)&desc5 >> 32) & 0xffffffff;
	
	desc5.sar_l = src_addr5 & 0xffffffff;
	desc5.sar_h = src_addr5 >> 32;
	desc5.dar_l = dst_addr5 & 0xffffffff;
	desc5.dar_h = dst_addr5 >> 32;
	desc5.block_ts = len5;
	val = axidma_reg_read(ch_base + CH_CTL_L);
	val = 0x00 << 11 | 0x00 << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0 << 0;
	desc5.ctrl_l = val;
	val = axidma_reg_read(ch_base + CH_CTL_H);
	val |= 0x01 << 31 | 0x01 << 30 | 0x1 << 26; // IOC_BlkTfr	
	desc5.ctrl_h = val;
	
	
	//printf("%p %p %p\n",(uint64_t)&desc1, (uint64_t)&desc2, (uint64_t)&desc3);
	//axidma_reg_write(ch_base + CH_LLP_L, ((uint64_t)0x200020000) & 0xffffffff & (~0x3f));
	//axidma_reg_write(ch_base + CH_LLP_L, 0x20000);
	axidma_reg_write(ch_base + CH_LLP_L, ((uint64_t)&desc1) & 0xffffffff & (~0x3f));
	//axidma_reg_write(ch_base + CH_LLP_H, (((uint64_t)0x200020000) >> 32 )& 0xffffffff);
	//axidma_reg_write(ch_base + CH_LLP_H, 0x02);
	axidma_reg_write(ch_base + CH_LLP_H, (((uint64_t)&desc1) >> 32 )& 0xffffffff);

	printf("sar_l=0x%x\n",desc1.sar_l);
	printf("sar_h=0x%x\n",desc1.sar_h);
	printf("dar_l=0x%x\n",desc1.dar_l);
	printf("dar_h=0x%x\n",desc1.dar_h);
	printf("block_ts=0x%x\n",desc1.block_ts);
	printf("llp_l=0x%x\n",desc1.llp_l);
	printf("llp_h=0x%x\n",desc1.llp_h);
	printf("ctrl_l=0x%x\n",desc1.ctrl_l);
	printf("ctrl_h=0x%x\n",desc1.ctrl_h);
	printf("sstat=0x%x\n",desc1.sstat);
	printf("dstat=0x%x\n",desc1.dstat);
	printf("llp_status_l=0x%x\n",desc1.llp_status_l);
	printf("llp_status_h=0x%x\n",desc1.llp_status_h);
	
	// 4. 使能通道 DMAC_ChEn
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (chann << 0 | chann << 8);
	axidma_reg_write(base + DMAC_CHEN_L, val);
	//printf("DMAC_CHEN_L:0x%x = 0x%x in %s\n",base + DMAC_CHEN_L, val, __func__);
	
	usleep(1000);
	// 5. poll方式等待传输完成 BLOCK_TFR_DONE
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS4:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
		if((val & 0x3) == 0x3)
		{
			break;
		}
	}
	
	for(i=0;i<len1;i++)
	{
		if(*(unsigned char *)(src_addr1+i) != *(unsigned char *)(dst_addr1+i))
		{
			error_flg++;
			printf("i=%d, src_addr1=0x%x != dst_addr1=0x%x\n",i, *(unsigned char *)(src_addr1+i), *(unsigned char *)(dst_addr1+i));
		}
	}
	
	for(i=0;i<len2;i++)
	{
		if(*(unsigned char *)(src_addr2+i) != *(unsigned char *)(dst_addr2+i))
		{
			error_flg++;
			printf("i=%d, src_addr2=0x%x != dst_addr2=0x%x\n",i, *(unsigned char *)(src_addr2+i), *(unsigned char *)(dst_addr2+i));
		}
	}
	
	for(i=0;i<len3;i++)
	{
		if(*(unsigned char *)(src_addr3+i) != *(unsigned char *)(dst_addr3+i))
		{
			error_flg++;
			printf("i=%d, src_addr3=0x%x != dst_addr3=0x%x\n",i, *(unsigned char *)(src_addr3+i), *(unsigned char *)(dst_addr3+i));
		}
	}
	
	for(i=0;i<len4;i++)
	{
		if(*(unsigned char *)(src_addr4+i) != *(unsigned char *)(dst_addr4+i))
		{
			error_flg++;
			printf("i=%d, src_addr4=0x%x != dst_addr4=0x%x\n",i, *(unsigned char *)(src_addr4+i), *(unsigned char *)(dst_addr4+i));
		}
	}
	
	for(i=0;i<len5;i++)
	{
		if(*(unsigned char *)(src_addr5+i) != *(unsigned char *)(dst_addr5+i))
		{
			error_flg++;
			printf("i=%d, src_addr5=0x%x != dst_addr5=0x%x\n",i, *(unsigned char *)(src_addr5+i), *(unsigned char *)(dst_addr5+i));
		}
	}
	
	
	if(error_flg == 0)
		printf("%s ------------------ pass\n",__func__);
	else
		printf("%s ------------------ fail\n",__func__);
}

	
int dmac_memcpy_lli_func05(unsigned int id, unsigned int chann, unsigned int width)
{	
	int i,n;
	
	unsigned int val;
	unsigned int base;
	unsigned int ch_base;
	unsigned int error_flg;
	struct axidma_config config;
	unsigned int transfer_width=0;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	

	//for(i=0;i<0x800;i++)
	//	*(unsigned char *)(0x200000000+i) = i;
	
	config.id = id;
	config.chann = chann;
	for(i=0;i<10;i++)
	{
		config.len[i] = 0x1000;
		config.src_addr[i] = 0x210000000 + i*config.len[i]*2;
		config.dst_addr[i] = 0x210700000 + i*config.len[i]*2;
		memset((unsigned char *)config.src_addr[i], i+1, 0x1000);
	}
	config.nums = 10;
	config.width = width;
	config.prior = 1;
	config.intr_en = 1;
	__axidma_config(config, desc1);
	dmac_start(id,chann);
	
	
	base = AXIDMA1_BASE_ADDR + (config.id -1)* 0x10000;
	ch_base = base + 0x100 + (config.chann -1) * 0x100;
	
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
		if((val & 0x3) == 0x3)
		{
			break;
		}
	}
	
	for(n=0;n<10;n++)
	{
		for(i=0;i<config.len[n] * transfer_width/8;i++)
		{
			if(*(unsigned char *)(config.src_addr[n]+i) != *(unsigned char *)(config.dst_addr[n]+i))
			{
				error_flg++;
				printf("n=%d, i=%d, src_addr=0x%x != dst_addr=0x%x\n",n, i, *(unsigned char *)(config.src_addr[n]+i), *(unsigned char *)(config.dst_addr[n]+i));
			}
		}
	}
	
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}


int dmac_memcpy_lli_func06(unsigned int id, unsigned int chann1, unsigned int chann2, unsigned int width)
{	
	int i,n;
	unsigned int val;
	unsigned int base;
	unsigned int ch_base1;
	unsigned int ch_base2;
	unsigned int error_flg;
	struct axidma_config config1;
	struct axidma_config config2;
	unsigned int transfer_width=0;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}

	//for(i=0;i<0x800;i++)
	//	*(unsigned char *)(0x200000000+i) = i;
	
	config1.id = id;
	config1.chann = chann1;
	for(i=0;i<15;i++)
	{
		config1.len[i] = 0x10000;
		config1.src_addr[i] = 0x210000000 + i*config1.len[i];
		config1.dst_addr[i] = 0x212000000 + i*config1.len[i];
		memset((unsigned char *)config1.src_addr[i], i+1, 0x100000);
	}
	config1.nums = 15;
	config1.width = width;
	config1.prior = 3;
	config1.intr_en = 1;
	__axidma_config(config1, desc1);
	
	
	config2.id = id;
	config2.chann = chann2;
	for(i=0;i<15;i++)
	{
		config2.len[i] = 0x10000;
		config2.src_addr[i] = 0x213000000 + i*config2.len[i];
		config2.dst_addr[i] = 0x215000000 + i*config2.len[i];
		memset((unsigned char *)config2.src_addr[i], i+1, 0x100000);
	}
	config2.nums = 15;
	config2.width = width;
	config2.prior = 2;
	config2.intr_en = 1;
	__axidma_config(config2, desc2);
	
	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	ch_base1 = base + 0x100 + (chann1 -1) * 0x100;
	ch_base2 = base + 0x100 + (chann2 -1) * 0x100;
	
	// 使能通道的block和dma完成中断
	axidma_reg_write(ch_base1 + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base1 + CH_INTSIGNAL_ENA, 0x03);
	
	axidma_reg_write(ch_base2 + CH_INTSTATUS_ENA, 0x03);
	axidma_reg_write(ch_base2 + CH_INTSIGNAL_ENA, 0x03);
	
	val = axidma_reg_read(base + DMAC_CFG);
	val = val | (0x01 << 0); // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);
	
	val = axidma_reg_read(base + DMAC_CHEN_L);
	val |= (0x1 << (chann1-1) | 0x01 << (chann1-1+8));
	val |= (0x1 << (chann2-1) | 0x01 << (chann2-1+8));
	axidma_reg_write(base + DMAC_CHEN_L, val);
	
	while(1)
	{
		unsigned ret1;
		unsigned ret2;
		
		ret1 = axidma_reg_read(ch_base1 + CH_INTSTATUS);
		ret2 = axidma_reg_read(ch_base2 + CH_INTSTATUS);
		//printf("chann1=0x%x chann2=0x%x in %s\n",ret1, ret2, __func__);
		if(((ret1 & 0x3) == 0x3) || ((ret2 & 0x3) == 0x3))
		{
			printf("chann1=0x%x chann2=0x%x in %s\n",ret1, ret2, __func__);
		}
		
		if(((ret1 & 0x3) == 0x3) && ((ret2 & 0x3) == 0x3))
		{
			printf("done in %s\n",__func__);
			break;
		}
	}
	
	for(n=0;n<10;n++)
	{
		for(i=0;i<config1.len[n] * transfer_width/8;i++)
		{
			if(*(unsigned char *)(config1.src_addr[n]+i) != *(unsigned char *)(config1.dst_addr[n]+i))
			{
				error_flg++;
				printf("id=%d, chann1=%d, i=%d, n=%d, src_addr=0x%x != dst_addr=0x%x\n",id, chann1, i, n, *(unsigned char *)(config1.src_addr[n]+i), *(unsigned char *)(config1.dst_addr[n]+i));
			}
		}
	}
	
	for(n=0;n<10;n++)
	{
		for(i=0;i<config2.len[n] * transfer_width/8;i++)
		{
			if(*(unsigned char *)(config2.src_addr[n]+i) != *(unsigned char *)(config2.dst_addr[n]+i))
			{
				error_flg++;
				printf("id=%d, chann2=%d, i=%d, n=%d, src_addr=0x%x != dst_addr=0x%x\n",id, chann2, i, n, *(unsigned char *)(config2.src_addr[n]+i), *(unsigned char *)(config2.dst_addr[n]+i));
			}
		}
	}
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}

void dmac_stress_lli_func06(unsigned int id, unsigned int chann_cnt, unsigned int width, uint64_t src_addr, uint64_t dst_addr, unsigned int len)
{
	int sg_nums, i, k;
	unsigned int val;
	unsigned int base;
	unsigned int ch_base[8];
	unsigned int ch_len;
	uint64_t src_sub, dst_sub;
	unsigned int chann[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	struct axidma_config config[8];

	if (chann_cnt > 8)
		chann_cnt = 8;
	ch_len = len / chann_cnt;
	sg_nums = (1UL << width) * MAX_BLOCK_TRANSFER_SIZE * MAX_DESC_NUMS / ch_len;
	if (sg_nums > MAX_SG_NUMS)
		sg_nums = MAX_SG_NUMS;

	for (k = 0; k < chann_cnt; k++) {
		config[k].id = id;
		config[k].chann = chann[k];
		src_sub = src_addr + k * ch_len;
		dst_sub = dst_addr + k * ch_len;
		for(i=0;i<sg_nums;i++)
		{
			config[k].len[i] = ch_len;
			config[k].src_addr[i] = i % 2 == 0 ? src_sub : dst_sub;
			config[k].dst_addr[i] = i % 2 == 0 ? dst_sub : src_sub;
		}
		config[k].nums = sg_nums;
		config[k].width = width;
		config[k].prior = 1;
		config[k].intr_en = 1;
		__axidma_config(config[k], desc8[k]);
	}

	base = AXIDMA1_BASE_ADDR + (id -1)* 0x10000;
	for (k = 0; k < chann_cnt; k++) {
		ch_base[k] = base + 0x100 + (chann[k] -1) * 0x100;
		// 使能通道的block和dma完成中断
		axidma_reg_write(ch_base[k] + CH_INTSTATUS_ENA, 0x03);
		axidma_reg_write(ch_base[k] + CH_INTSIGNAL_ENA, 0x03);
	}

	val = axidma_reg_read(base + DMAC_CFG);
	val = val | (0x01 << 0); // DMAC_EN
	axidma_reg_write(base + DMAC_CFG, val);

	val = axidma_reg_read(base + DMAC_CHEN_L);
	for (k = 0; k < chann_cnt; k++) {
		val |= (0x1 << (chann[k]-1) | 0x01 << (chann[k]-1+8));
	}
	axidma_reg_write(base + DMAC_CHEN_L, val);
#if SHOW_DDR_PERF
	pm_start_port(PORT4);
#endif

	for (k = 0; k < chann_cnt; k++) {
		while(1) {
			val = axidma_reg_read(ch_base[k] + CH_INTSTATUS);
			if((val & 0x3) == 0x3)
			{
				// printf("chann %u done\n", chann[k]);
				break;
			}
		}
	}
#if SHOW_DDR_PERF
	pm_end_port();
#endif
}

void dmac_memcpy_die_func07(unsigned char die_id_src, unsigned char die_id_dst, unsigned int id, unsigned int chann, unsigned int len, unsigned int width)
{
	int ret;
	#define DIE_OFFSET 0x400000000
	
	// sram to sram
	ret = dmac_memcpy_func02(id, chann, 0x60060000+die_id_src*DIE_OFFSET, 0x60060000+die_id_dst*DIE_OFFSET, len, width);
	if(ret < 0)
	{
		printf("id=%d,chann=%d,len=%x,width=%d die%d(sram) to die%d(sram) .......... fail\n",id, chann, len, width, die_id_src, die_id_dst);
	}
	printf("id=%d,chann=%d,len=%x,width=%d die%d(sram) to die%d(sram) .......... ok\n",id, chann, len, width, die_id_src, die_id_dst);
	
	
	// sram to ddr
	ret = dmac_memcpy_func02(id, chann, 0x60060000+die_id_src*DIE_OFFSET, 0x260060000+die_id_dst*DIE_OFFSET, len, width);
	if(ret < 0)
	{
		printf("id=%d,chann=%d,len=%x,width=%d die%d(sram) to die%d(ddr) .......... fail\n",id, chann, len, width, die_id_src, die_id_dst);
	}
	printf("id=%d,chann=%d,len=%x,width=%d die%d(sram) to die%d(ddr) .......... ok\n",id, chann, len, width, die_id_src, die_id_dst);
	
	
	// ddr to sram
	ret = dmac_memcpy_func02(id, chann, 0x260060000+die_id_src*DIE_OFFSET, 0x60060000+die_id_dst*DIE_OFFSET, len, width);
	if(ret < 0)
	{
		printf("id=%d,chann=%d,len=%x,width=%d die%d(ddr) to die%d(sram) .......... fail\n",id, chann, len, width, die_id_src, die_id_dst);
	}
	printf("id=%d,chann=%d,len=%x,width=%d die%d(ddr) to die%d(sram) .......... ok\n",id, chann, len, width, die_id_src, die_id_dst);
	
	
	// ddr to ddr
	ret = dmac_memcpy_func02(id, chann, 0x260060000+die_id_src*DIE_OFFSET, 0x260060000+die_id_dst*DIE_OFFSET, len, width);
	if(ret < 0)
	{
		printf("id=%d,chann=%d,len=%x,width=%d die%d(ddr) to die%d(ddr) .......... fail\n",id, chann, len, width, die_id_src, die_id_dst);
	}
	printf("id=%d,chann=%d,len=%x,width=%d die%d(ddr) to die%d(ddr) .......... ok\n",id, chann, len, width, die_id_src, die_id_dst);
}


void dmac_memcpy_die_test(void)
{
	int i;
	int id, len, chann, width;

	#if 0
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<9;chann++)
		{
			for(width=0;width<5;width++)
			{
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				{
					dmac_memcpy_die_func07(1, 7, id, chann, len, width);
				}
			}
		}
	}
	#endif
	
	
	#if 1
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<9;chann++)
		{
			for(width=0;width<5;width++)
			{
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				{
					dmac_memcpy_die_func07(1, 0, id, chann, len, width);
				}
			}
		}
	}
	#endif
	
	#if 0
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<9;chann++)
		{
			for(width=0;width<5;width++)
			{
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				{
					dmac_memcpy_die_func07(0, 3, id, chann, len, width);
				}
			}
		}
	}
	#endif
	
	#if 0
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<9;chann++)
		{
			for(width=0;width<5;width++)
			{
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				{
					dmac_memcpy_die_func07(1, 3, id, chann, len, width);
				}
			}
		}
	}
	#endif
}


void dmac_memcpy_perf_test(void)
{
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x10, 0);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x100, 0);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x1000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x10000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x20000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x30000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x40000, 0);
	
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x10, 4);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x100, 4);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x1000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x10000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x20000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x30000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x210000000, 0x220000000, 0x40000, 4);
	
	
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x10, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x100, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x1000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x10000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x20000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x30000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x38000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x40000, 0);
	
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x10, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x100, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x1000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x2000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x3000, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x3800, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x220000000, 0x4000, 4);
	
	
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x10, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x100, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x1000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x2000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x5000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x8000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0xa000, 0);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x10000, 0);
	
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x10, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x100, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x200, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x500, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x800, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0xa00, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0xd00, 4);
	dmac_memcpy_func02_perf(1, 1, 0x60030000, 0x60050000, 0x1000, 4);
	printf("axidma perf test done!!!\n");
}

void dmac_memcpy_max_perf_test(void)
{
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x10, 0);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x100, 0);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x1000, 0);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x10000, 0);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x20000, 0);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x30000, 0);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x40000, 0);
	                   
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x10, 4);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x100, 4);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x1000, 4);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x10000, 4);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x20000, 4);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x30000, 4);
	dmac_memcpy_func02_max_perf(1, 0x210000000, 0x220000000, 0x40000, 4);
	                   
	                   
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x10, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x100, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x1000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x10000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x20000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x30000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x38000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x40000, 0);
	                   
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x10, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x100, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x1000, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x2000, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x3000, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x3800, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x220000000, 0x4000, 4);
	                   
	                   
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x10, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x100, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x1000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x2000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x5000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x8000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0xa000, 0);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x10000, 0);
	                   
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x10, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x100, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x200, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x500, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x800, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0xa00, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0xd00, 4);
	dmac_memcpy_func02_max_perf(1, 0x60030000, 0x60050000, 0x1000, 4);
	printf("axidma perf test done!!!\n");
}

void dmac_memcpy_max_perf_two_axidma_test(void)
{
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x10, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x100, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x1000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x10000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x20000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x30000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x40000, 0);
	                   
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x10, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x100, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x1000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x10000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x20000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x30000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x210000000, 0x220000000, 0x40000, 4);
	                   
	                   
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x10, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x100, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x1000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x10000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x20000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x30000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x38000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x40000, 0);
	                   
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x10, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x100, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x1000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x2000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x3000, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x3800, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x220000000, 0x4000, 4);
	                   
	                   
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x10, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x100, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x1000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x2000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x5000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x8000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0xa000, 0);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x10000, 0);
	                   
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x10, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x100, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x200, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x500, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x800, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0xa00, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0xd00, 4);
	dmac_memcpy_func02_two_axidma_max_perf(0x60030000, 0x60050000, 0x1000, 4);
	printf("axidma perf test done!!!\n");
}


void ap_sram_userid_set(unsigned short authority)
{
	unsigned int value = 0;

	value = authority<<16|authority;
	//write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_REGION0_AUTHORITY_ADDR, value);
	
	write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_REGION0_AUTHORITY_ADDR, 0xff7fff7f); // abnormal，确保a55可以使用
	//write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_REGION0_AUTHORITY_ADDR, 0xffffffff); // ok

	value = FIREWALL_SLAVE_HIDE_ERR_DIS | FIREWALL_SLAVE_EN;
	//value = FIREWALL_SLAVE_HIDE_ERR_EN | FIREWALL_SLAVE_EN;
	write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_ENABLE_ADDR, value);
}

static void ap_sram_secure_set(unsigned char secure)
{
	unsigned int value = 0;
	if(secure) {
		value = FIREWALL_REGION_ATTR_RD_SEC\
			| FIREWALL_REGION_ATTR_WR_SEC\
			| FIREWALL_REGION_ATTR_EN;
		write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_REGION0_ATTRIBUTE_ADDR, value);
	} else {
		value = FIREWALL_REGION_ATTR_RD_NONSEC\
			| FIREWALL_REGION_ATTR_WR_NONSEC\
			| FIREWALL_REGION_ATTR_EN;
		write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_REGION0_ATTRIBUTE_ADDR, value);
	}

	//value = FIREWALL_SLAVE_HIDE_ERR_DIS | FIREWALL_SLAVE_EN;
	value = FIREWALL_SLAVE_HIDE_ERR_EN | FIREWALL_SLAVE_EN;
	write_mreg32(FIREWALL_AP_AP_SRAM_S5_F0_ENABLE_ADDR, value);
}

static int __axidma_master_firewall_ddr_test(unsigned int id, unsigned int ddr_authority,
				unsigned int axidma_authority, unsigned int ddr_secure, unsigned int axidma_secure)
{
	int ret;
#if 1	
	firewall_ap2ddr_authority_set(0x01 << ddr_authority);
	if(id == 1)
		firewall_master_userid_set(MASTER_AXI_DMA0_USER, axidma_authority);
	else if(id == 2)
		firewall_master_userid_set(MASTER_AXI_DMA1_USER, axidma_authority);
	
	firewall_ap2ddr_secure_set(ddr_secure);
	if(id == 1)
		firewall_master_secure_set(MASTER_AXI_DMA0_USER, axidma_secure);	
	else if(id == 2)
		firewall_master_secure_set(MASTER_AXI_DMA1_USER, axidma_secure);
#endif
	if(id == 1)
		ret = dmac_memcpy_func02_timeout(1, 1, 0x300000000, 0x3FFFFFFF0, 0xf, 0, 10); 
	else if(id == 2)
		ret = dmac_memcpy_func02_timeout(2, 1, 0x300000000, 0x3FFFFFFF0, 0xf, 0, 10); 
	
	if(ret < 0)
		return -1;
	else
		return 0;
}

void axidma_master_firewall_ddr_test(unsigned int id)
{
	if(__axidma_master_firewall_ddr_test(id, 8, 8, 0, 0) != 0)
		printf("~~~~~~fail to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 0, 0);
	else
		printf("~~~~~~success to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 0, 0);
	
	if(__axidma_master_firewall_ddr_test(id, 7, 8, 0, 0) != 0)
		printf("~~~~~~success to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 7, 8, 0, 0);
	else
		printf("~~~~~~fail to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 0, 0);
	
	if(__axidma_master_firewall_ddr_test(id, 8, 8, 0, 1) != 0)
		printf("~~~~~~fail to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 0, 1);
	else
		printf("~~~~~~success to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 0, 1);
	
	if(__axidma_master_firewall_ddr_test(id, 8, 8, 1, 0) != 0)
		printf("~~~~~~success to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 1, 0);
	else
		printf("~~~~~~fail to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 1, 0);
	
	if(__axidma_master_firewall_ddr_test(id, 8, 8, 1, 1) != 0)
		printf("~~~~~~fail to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 1, 1);
	else
		printf("~~~~~~success to test axidma%d to ddr firewall(%d %d %d %d)\n",id, 8, 8, 1, 1);
	
}

static int __axidma_master_firewall_sram_test(unsigned int id, unsigned int sram_authority,
				unsigned int axidma_authority, unsigned int sram_secure, unsigned int axidma_secure)
{
	int ret;
	
	ap_sram_userid_set(0x01 << sram_authority);
	if(id == 1)
		firewall_master_userid_set(MASTER_AXI_DMA0_USER, axidma_authority); // just 0x07 will fail
	else if(id == 2)
		firewall_master_userid_set(MASTER_AXI_DMA1_USER, axidma_authority); // just 0x07 will fail

	ap_sram_secure_set(sram_secure);	
	if(id == 1)
		firewall_master_secure_set(MASTER_AXI_DMA0_USER, axidma_secure);	
	else if(id == 2)
		firewall_master_secure_set(MASTER_AXI_DMA1_USER, axidma_secure);
	
	if(id == 1)
		ret = dmac_memcpy_func02_timeout(1, 1, 0x60000000, 0x6007FFF0, 0xf, 0, 10);
	else if(id == 2)
		ret = dmac_memcpy_func02_timeout(2, 1, 0x60000000, 0x6007FFF0, 0xf, 0, 10);
	
	if(ret < 0)
		return -1;
	else
		return 0;
}

void axidma_master_firewall_sram_test(unsigned int id)
{
	if(__axidma_master_firewall_sram_test(id, 8, 8, 0, 0) != 0)
		printf("~~~~~~fail to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 0, 0);
	else
		printf("~~~~~~success to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 0, 0);
	
	if(__axidma_master_firewall_sram_test(id, 8, 7, 0, 0) != 0)
		printf("~~~~~~success to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 7, 0, 0);
	else
		printf("~~~~~~fail to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 7, 0, 0);
	
	if(__axidma_master_firewall_sram_test(id, 8, 8, 0, 1) != 0)
		printf("~~~~~~fail to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 0, 1);
	else
		printf("~~~~~~success to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 0, 1);
	
	if(__axidma_master_firewall_sram_test(id, 8, 8, 1, 0) != 0)
		printf("~~~~~~success to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 1, 0);
	else
		printf("~~~~~~fail to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 1, 0);
	
	if(__axidma_master_firewall_sram_test(id, 8, 8, 1, 1) != 0)
		printf("~~~~~~fail to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 1, 1);
	else
		printf("~~~~~~success to test axidma%d to sram firewall(%d %d %d %d)\n",id, 8, 8, 1, 1);
}


static void firewall_ap2d2d1_authority_set(unsigned short authority)
{
	unsigned int mask = 0, value = 0;

	value = (authority&0xffff)<<16 | (authority&0xffff);
	write_mreg32(FIREWALL_AP_AP_D2D_S18_F0_REGION0_AUTHORITY_ADDR, value);

	mask = FIREWALL_AP_AP_D2D_S18_F0_REGION0_EN_MASK;
	value = mask;
	reg32_update(FIREWALL_AP_AP_D2D_S18_F0_REGION0_ATTRIBUTE_ADDR, mask, value);

	mask = FIREWALL_AP_AP_D2D_S18_F0_EN_MASK;
	value = mask;
	reg32_update(FIREWALL_AP_AP_D2D_S18_F0_ENABLE_ADDR, mask, value);
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S18_F0_REGION0_AUTHORITY_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S18_F0_REGION0_AUTHORITY_ADDR));
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S18_F0_REGION0_ATTRIBUTE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S18_F0_REGION0_ATTRIBUTE_ADDR));
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S18_F0_ENABLE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S18_F0_ENABLE_ADDR));
}

static void firewall_ap2d2d1_secure_set(unsigned char secure)
{
	unsigned int mask = 0, value = 0;

	mask = FIREWALL_AP_AP_D2D_S18_F0_REGION0_RD_SEC_ATTR_MASK|FIREWALL_AP_AP_D2D_S18_F0_REGION0_WR_SEC_ATTR_MASK |FIREWALL_AP_AP_D2D_S18_F0_REGION0_EN_MASK;
	if(secure) {
		value = FIREWALL_AP_AP_D2D_S18_F0_REGION0_EN_MASK;
	} else {
		value = mask;
	}
	reg32_update(FIREWALL_AP_AP_D2D_S18_F0_REGION0_ATTRIBUTE_ADDR, mask, value);

	mask = FIREWALL_AP_AP_D2D_S18_F0_EN_MASK;
	value = mask;
	reg32_update(FIREWALL_AP_AP_D2D_S18_F0_ENABLE_ADDR, mask, value);
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S18_F0_REGION0_ATTRIBUTE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S18_F0_REGION0_ATTRIBUTE_ADDR));
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S18_F0_ENABLE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S18_F0_ENABLE_ADDR));
}

static void firewall_ap2d2d2_authority_set(unsigned short authority)
{
	unsigned int mask = 0, value = 0;

	value = (authority&0xffff)<<16 | (authority&0xffff);
	write_mreg32(FIREWALL_AP_AP_D2D_S19_F0_REGION0_AUTHORITY_ADDR, value);

	mask = FIREWALL_AP_AP_D2D_S19_F0_REGION0_EN_MASK;
	value = mask;
	reg32_update(FIREWALL_AP_AP_D2D_S19_F0_REGION0_ATTRIBUTE_ADDR, mask, value);

	mask = FIREWALL_AP_AP_D2D_S19_F0_EN_MASK;
	value = mask;
	reg32_update(FIREWALL_AP_AP_D2D_S19_F0_ENABLE_ADDR, mask, value);
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S19_F0_REGION0_AUTHORITY_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S19_F0_REGION0_AUTHORITY_ADDR));
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S19_F0_REGION0_ATTRIBUTE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S19_F0_REGION0_ATTRIBUTE_ADDR));
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S19_F0_ENABLE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S19_F0_ENABLE_ADDR));
}

static void firewall_ap2d2d2_secure_set(unsigned char secure)
{
	unsigned int mask = 0, value = 0;

	mask = FIREWALL_AP_AP_D2D_S19_F0_REGION0_RD_SEC_ATTR_MASK|FIREWALL_AP_AP_D2D_S19_F0_REGION0_WR_SEC_ATTR_MASK |FIREWALL_AP_AP_D2D_S19_F0_REGION0_EN_MASK;
	if(secure) {
		value = FIREWALL_AP_AP_D2D_S19_F0_REGION0_EN_MASK;
	} else {
		value = mask;
	}
	reg32_update(FIREWALL_AP_AP_D2D_S19_F0_REGION0_ATTRIBUTE_ADDR, mask, value);

	mask = FIREWALL_AP_AP_D2D_S19_F0_EN_MASK;
	value = mask;
	reg32_update(FIREWALL_AP_AP_D2D_S19_F0_ENABLE_ADDR, mask, value);
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S19_F0_REGION0_ATTRIBUTE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S19_F0_REGION0_ATTRIBUTE_ADDR));
	printf("%s Addr - 0x%08x, value - 0x%08x\n ", __func__,
		FIREWALL_AP_AP_D2D_S19_F0_ENABLE_ADDR, read_mreg32(FIREWALL_AP_AP_D2D_S19_F0_ENABLE_ADDR));
}

static void a55_non_secure(void)                                                 
{                                                                               
#ifdef ARCH_A55                                                                 
	armv8_switch_to_el2();                                                  
	armv8_switch_to_el1();                                                  
	local_serror_enable();                                                  
#endif                                                                          
}

/* AON_SRAM: 0x2000_0000 - 0x2003_FFFF */
/* AP_SRAM: 0x6000_0000 - 0x6007_FFFF  */
/* DDR: 0x2_0000_0000 - 0x3_FFFF_FFFF  */
void axidma_test(void)
{
	int id, len, chann, width;
	int ret;
	int error_flg=0;
	uint64_t src,dst;
	
	intc_register_done1 = 0;
	intc_register_done2 = 0;
	
	dmac_init(1);
	dmac_init(2);
		
	#if 0 // 低频率测试
		unsigned int val;
		val = read_mreg32(AP_GEN_SEL_ADDR);
		val &= (~AP_GEN_SEL_CLK_AP_AXI_MASK);
		val &= (~AP_GEN_SEL_CLK_AP_HIGH_PERI_MASK);
		write_mreg32(AP_GEN_SEL_ADDR, val);

		val = read_mreg32(AP_GEN_SEL_2_ADDR);
		val &= ~(AP_GEN_SEL_CLK_AP_AXI_MAIN_MASK);
		write_mreg32(AP_GEN_SEL_2_ADDR, val);
	#else // 高频率测试
		unsigned int val;
		val = read_mreg32(AP_GEN_SEL_ADDR);
		val |= (AP_GEN_SEL_CLK_AP_AXI_MASK);
		val |= (AP_GEN_SEL_CLK_AP_HIGH_PERI_MASK);
		write_mreg32(AP_GEN_SEL_ADDR, val);

		val = read_mreg32(AP_GEN_SEL_2_ADDR);
		val |= (AP_GEN_SEL_CLK_AP_AXI_MAIN_MASK);
		write_mreg32(AP_GEN_SEL_2_ADDR, val);
	#endif
	
	#if 1
		ret = dmac_reg_rw_func01(1);
		if(ret == 0)
			printf("id=%d reg test ........ ok\n",1);
		else
			printf("id=%d reg test ........ fail\n",1);
	#endif
	
	#if 1
		ret = dmac_reg_rw_func01(2);
		if(ret == 0)
			printf("id=%d reg test ........ ok\n",2);
		else
			printf("id=%d reg test ........ fail\n",2);
	#endif
	
	#if 0 // firewall 测试
		axidma_master_firewall_ddr_test(1);
		axidma_master_firewall_ddr_test(2);
		axidma_master_firewall_sram_test(1);
		axidma_master_firewall_sram_test(2);
	#endif
	
	#if 1 // 地址边界测试
		// ap sram 0x60000000~0x60080000
		printf("0x6007FFF0=\t");
		for(int i=0;i<0x10;i++)
			printf("0x%x ",*(unsigned char *)(0x6007FFF0UL+i));
		printf("\n");
		ret = dmac_memcpy_func02(1, 1, 0x60000000, 0x6007FFF0, 0xf, 0);
		if(ret < 0)
		{
			printf("dmac_memcpy_func02(from 0x60000000 to 0x6007FFF0) .......... fail\n");
			return;
		}
		printf("0x60000000=\t");
		for(int i=0;i<0x10;i++)
			printf("0x%x ",*(unsigned char *)(0x60000000UL+i));
		printf("\n");
		printf("0x6007FFF0=\t");
		for(int i=0;i<0x10;i++)
			printf("0x%x ",*(unsigned char *)(0x6007FFF0UL+i));
		printf("\n");

		// ap sram 0x200000000~0x400000000
		printf("0x2FFFFFFF0=\t");
		for(int i=0;i<0x10;i++)
			printf("0x%x ",*(unsigned char *)(0x2FFFFFFF0+i));
		printf("\n");
		ret = dmac_memcpy_func02(1, 1, 0x200000000, 0x2FFFFFFF0, 0xf, 0); // 0x10会死机
		if(ret < 0)
		{
			printf("dmac_memcpy_func02(from 0x200000000 to 0x2FFFFFFF0) .......... fail\n");
			return;
		}
		printf("0x200000000=\t");
		for(int i=0;i<0x10;i++)
			printf("0x%x ",*(unsigned char *)(0x200000000+i));
		printf("\n");
		printf("0x2FFFFFFF0=\t");
		for(int i=0;i<0x10;i++)
			printf("0x%x ",*(unsigned char *)(0x2FFFFFFF0+i));
		printf("\n");
	#endif 
	
	#if 0 //性能测试
		#if SHOW_DDR_PERF
			pm_set_master(PORT4, AXIDMA_USER);
		#endif
			dmac_memcpy_perf_test();
			dmac_memcpy_max_perf_test();
			dmac_memcpy_max_perf_two_axidma_test();
	#endif
	
	#if 0 // 多die测试
		dmac_memcpy_die_test();
		while(1);
	#endif
	
	#if 0 // 多die的firewall测试
		id = 2;
		printf("boot_strap.chip_id=%d\n",boot_strap.chip_id);
		if(boot_strap.chip_id == 0) 
		{
			if(id == 1)
			{
				//a55_non_secure();
				firewall_master_userid_set(MASTER_A55_AXI0_USER, 0x01);
				firewall_master_userid_set(MASTER_AXI_DMA0_USER, 0x07);
				firewall_master_secure_set(MASTER_AXI_DMA0_USER, 1);
			}
			else if(id == 2)
			{
				firewall_master_userid_set(MASTER_A55_AXI0_USER, 0x01);
				firewall_master_userid_set(MASTER_AXI_DMA1_USER, 0x07);
				firewall_master_secure_set(MASTER_AXI_DMA1_USER, 1);
			}
			firewall_ap2d2d1_authority_set((0x1<<8) | (0x1<<1));
			firewall_ap2d2d1_secure_set(1);
			
			dmac_memcpy_die_func07(0, 1, id, 1, 0x1000, 0);
			printf("multi die%d firewall test done\n",boot_strap.chip_id);
			while(1);
		}
		else if(boot_strap.chip_id == 1)
		{
			//firewall_ap2d2d_authority_set(0x1<<8);
			//firewall_ap2d2d_secure_set(1);
			printf("multi die%d firewall test done\n",boot_strap.chip_id);
			while(1);
		}
		
	#endif 
	
	#if 0 //aon中断测试  axidma0-1 -------> aon intc 99-100
		// 1. star Makefile 关掉DCONFIG_INTC_DIS
		// 2. 在star的main中添加axidma1_aon_irq_enable() 以及 axidma2_aon_irq_enable()
		// 3. 编译star, make clean && make ARCH=star LOC=sram TARGET="aon_irq"
		// 4. 运行a55的axidma_test用例
		// 5. 确认star的串口是否有打印
		ret = dmac_interrupt_func03(1, 1, 0x241000000, 0x241009000, 0x800, 0);
		if(ret < 0)
		{
			printf("dmac_interrupt_func03 .......... fail\n");
		}
		printf("dmac_interrupt_func03 .......... ok\n");
		intc_disable_normal_irq(axidma_handler2.irq_id);
		while(1){;}
	#endif 
	
#if 0	
	#ifdef IS_EMU
		ret = dmac_memcpy_func02(1, 1, 0x210000000, 0x210090001, 0x100, 0);
		if(ret < 0)
		{
			printf("test1.1 .......... fail\n");
		}
		printf("test1.1 .......... ok\n");
		ret = dmac_memcpy_func02(1, 1, 0x60070000, 0x60072000, 0x100, 0);
		if(ret < 0)
		{
			printf("test1.2 .......... fail\n");
		}
		printf("test1.2 .......... ok\n");
		
		ret = dmac_memcpy_func02(1, 1, 0x60070000, 0x201090001, 0x100, 0);
		if(ret < 0)
		{
			printf("test1.3 .......... fail\n");
		}
		printf("test1.3 .......... ok\n");
			
		ret = dmac_memcpy_func02(2, 1, 0x201000001, 0x60070000, 0x101, 0);
		if(ret < 0)
		{
			printf("test1.4 .......... fail\n");
		}
		printf("test1.4 .......... ok\n");

		ret = dmac_interrupt_func03(1, 1, 0x201000000, 0x201009000, 0x800, 0);
		if(ret < 0)
		{
			printf("test1.5 .......... fail\n");
		}
		printf("test1.5 .......... ok\n");
		intc_disable_normal_irq(axidma_handler1.irq_id);
	#else
		ret = dmac_memcpy_func02(1, 2, 0x210000000, 0x210090001, 0x40000, 0);
		if(ret < 0)
		{
			printf("test1.1 .......... fail\n");
		}
		printf("test1.1 .......... ok\n");
		ret = dmac_memcpy_func02(1, 2, 0x60070000, 0x60072000, 0x1000, 0);
		if(ret < 0)
		{
			printf("test1.2 .......... fail\n");
		}
		printf("test1.2 .......... ok\n");
		
		ret = dmac_memcpy_func02(1, 2, 0x60070000, 0x201090001, 0x3000, 0);
		if(ret < 0)
		{
			printf("test1.3 .......... fail\n");
		}
		printf("test1.3 .......... ok\n");
			
		ret = dmac_memcpy_func02(2, 2, 0x201000001, 0x60070000, 0x401, 0);
		if(ret < 0)
		{
			printf("test1.4 .......... fail\n");
		}
		printf("test1.4 .......... ok\n");

		ret = dmac_interrupt_func03(1, 1, 0x201000000, 0x201009000, 0x800, 0);
		if(ret < 0)
		{
			printf("test1.5 .......... fail\n");
		}
		printf("test1.5 .......... ok\n");
		intc_disable_normal_irq(axidma_handler1.irq_id);
		
		ret = dmac_memcpy_func02(1, 1, 0x210000000, 0x210090001, 0x1, 0);
		if(ret < 0)
		{
			printf("test1.6 .......... fail\n");
		}
		printf("test1.6 .......... ok\n");
	#endif

#endif

#if 1
	#if 1
	// 遍历所有传输大小，总线位宽，通道，axidma设备（轮训模式）
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<9;chann++)
		{
			for(width=0;width<5;width++)
			{
				error_flg = 0;
				#ifdef IS_EMU
				len = 0x100;
				#else
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				#endif
				{
					ret = dmac_memcpy_func02(id, chann, 0x201000001, 0x201900003, len, width);
					//ret = dmac_memcpy_func02(id, chann, 0x200000000, 0x200090000, len, width);
					if(ret < 0)
					{
						error_flg++;
						printf("id=%d chann=%d width=%d len=%d, memcpy test ...... fail\n",id, chann, width, len);
					}
				}
				if(error_flg == 0)
					printf("id=%d chann=%d width=%d memcpy(poll) .......... ok\n",id, chann, width);
				else
					printf("id=%d chann=%d width=%d memcpy(poll) .......... fail\n",id, chann, width);
			}
		}
	}
	#endif
	

	
	#if 1
	// 遍历所有传输大小，总线位宽，通道，axidma设备（中断模式）
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<9;chann++)
		{
			for(width=0;width<5;width++)
			{
				error_flg = 0;
				#ifdef IS_EMU
				len = 0x100;
				#else
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				#endif
				{
					//ret = dmac_interrupt_func03(id, chann, 0x200000001, 0x200090003, len, width); 
					ret = dmac_interrupt_func03(id, chann, 0x201000000, 0x201900000, len, width); 
					if(ret < 0)
					{
						error_flg++;
						printf("id=%d chann=%d width=%d len=%d, memcpy test ...... fail\n",id, chann, width, len);
					}
				}
				if(error_flg == 0)
					printf("id=%d chann=%d width=%d memcpy(interrupt) .......... ok\n",id, chann, width);
				else
					printf("id=%d chann=%d width=%d memcpy(interrupt) .......... fail\n",id, chann, width);
			}
		}
	}
	intc_disable_normal_irq(axidma_handler1.irq_id);
	intc_disable_normal_irq(axidma_handler2.irq_id);
	
	#endif
	
	#if 1
	// 遍历所有传输大小，总线位宽（轮训+非对齐访问）
	for(src=0x201000000;src<0x201000008;src++)
	{
		for(dst=0x201900000;dst<0x201900008;dst++)
		{
			for(width=0;width<1;width++)
			{
				error_flg = 0;
				#ifdef IS_EMU
				len = 0x100;
				#else
				//for(len=1;len<0x40000;len+=0x10000)
				len = 0x100;
				#endif
				//for(len=0x1000;len<0x10000;len+=0x1000)
				{
					ret = dmac_memcpy_func02(1, 1, src, dst, len, width);
					if(ret < 0)
					{
						error_flg++;
						printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d len=0x%x, memcpy test ...... fail\n",src, dst, 1, 1, width, len);
					}
				}
				if(error_flg == 0)
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(unaligned) .......... ok\n",src, dst, 1, 1, width);
				else
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(unaligned) .......... fail\n",src, dst, 1, 1, width);
			}
		}
	}
	#endif
	
	#if 1
	// sram to ddr 
	for(src=0x60070000;src<0x60070008;src++)
	{
		for(dst=0x201900000;dst<0x201900008;dst++)
		{
			for(width=0;width<5;width++)
			{
				error_flg = 0;
				#ifdef IS_EMU
				len = 0x100;
				#else
				//for(len=0x100;len<0x1000;len+=0x100)
				len = 0x100;
				#endif
				{
					ret = dmac_memcpy_func02(1, 1, src, dst, 0x101, width);
					if(ret < 0)
					{
						error_flg++;
						printf("id=%d chann=%d width=%d len=%d, memcpy test ...... fail\n",1, 1,width, len);
					}
				}
				if(error_flg == 0)
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(sram_to_ddr) .......... ok\n",src, dst, 1, 1,width);
				else
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(sram_to_ddr) .......... fail\n",src, dst, 1, 1,width);
			}
		}
	}
	#endif
	
	
	#if 1
	// ddr to sram
	for(src=0x201020000;src<0x201020008;src++)
	{
		for(dst=0x60070000;dst<0x60070008;dst++)
		{
			for(width=0;width<5;width++)
			{
				error_flg = 0;
				#ifdef IS_EMU
				len = 0x100;
				#else
				//for(len=1;len<0x1000;len+=0x100)
				len = 0x100;
				#endif
				{
					ret = dmac_memcpy_func02(1, 1, src, dst, 0x101, width);
					if(ret < 0)
					{
						error_flg++;
						printf("id=%d chann=%d width=%d len=%d, memcpy test ...... fail\n",1, 1,width, len);
					}
				}
				if(error_flg == 0)
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(ddr_to_sram) .......... ok\n",src, dst, 1, 1,width);
				else
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(ddr_to_sram) .......... fail\n",src, dst, 1, 1,width);
			}
		}
	}
	#endif

	#if 1
	// sram to sram 
	for(src=0x60070000;src<0x60070008;src++)
	{
		for(dst=0x60079000;dst<0x60079008;dst++)
		{
			for(width=0;width<5;width++)
			{
				error_flg = 0;
				#ifdef IS_EMU
				len = 0x100;
				#else
				//for(len=0x100;len<0x1000;len+=0x100)
				len = 0x100;
				#endif
				{
					ret = dmac_memcpy_func02(1, 1, src, dst, 0x101, width);
					if(ret < 0)
					{
						error_flg++;
						printf("id=%d chann=%d width=%d len=%d, memcpy test ...... fail\n",1, 1,width, len);
					}
				}
				if(error_flg == 0)
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(sram_to_sram) .......... ok\n",src, dst, 1, 1,width);
				else
					printf("src=0x%llx dst=0x%llx id=%d chann=%d width=%d memcpy(sram_to_sram) .......... fail\n",src, dst, 1, 1,width);
			}
		}
	}
	#endif
	

	#if 1
	// 链式dma
	for(id=1;id<3;id++)
	{
		for(chann=1;chann<3;chann++)
		{
			for(width=0;width<5;width++)
			{
				ret = dmac_memcpy_lli_func05(id, chann, width);
				if(ret == 0)
					printf("id=%d chann=%d width=%d memcpy(lli) .......... ok\n",id, chann, width);
				else
					printf("id=%d chann=%d width=%d memcpy(lli) .......... fail\n",id, chann,width);
			}
		}
	}
	#endif
#endif	

	#if 1
	// 多通道同时工作+优先级
	ret = dmac_memcpy_lli_func06(1, 1, 2, BITS_8);
	if(ret == 0)
		printf("id=%d width=%d memcpy(mulit_chann+lli+prio) .......... ok\n",1, 0);
	else
		printf("id=%d width=%d memcpy(mulit_chann+lli+prio) .......... fail\n",1, 0);
	#endif
	
	#if 0
	dmac_memcpy_lli_func04_2(1, 1, 0x210000000, 0x210000800, 0x800, 0x210010000, 0x210010800, 0x800);
	printf("in axidma test 6\n");
	#endif
	
	#if 0
	dmac_memcpy_lli_func04_3(1, 1, 0x210000000, 0x210000800, 0x800, 0x210010000, 0x210010800, 0x800, 0x210020000, 0x200020800, 0x800,
	0x210030000, 0x210030800, 0x800, 0x210040000, 0x210040800, 0x800);
	printf("in axidma test 7\n");
	#endif

}
