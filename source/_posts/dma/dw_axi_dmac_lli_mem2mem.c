/*
 * DesignWare AXI DMAC - LLI (Linked List Item) Mode Memory-to-Memory Transfer
 *
 * This implementation follows the Programming Flow for Linked-List-Based Multi-Block Transfer
 * from the DesignWare DW_axi_dmac Databook (Section 7.2)
 *
 * LLI Structure (64 bytes, aligned to 64-byte boundary):
 *   Offset 0x00: CHx_SAR [63:0]   - Source Address
 *   Offset 0x08: CHx_DAR [63:0]   - Destination Address
 *   Offset 0x10: CHx_BLOCK_TS [31:0] - Block Transfer Size
 *   Offset 0x14: Reserved
 *   Offset 0x18: CHx_LLP [63:5]   - Next LLI Pointer (bits 63:6, lower 6 bits are 0)
 *   Offset 0x1C: Reserved
 *   Offset 0x20: CHx_CTL [63:0]   - Control Register
 *   Offset 0x28: CHx_SSTAT [63:0] - Source Status (optional write-back)
 *   Offset 0x38: CHx_DSTAT [63:0] - Destination Status (optional write-back)
 *   Offset 0x48: CHx_LLP_STATUS [63:0] - LLI Status (optional write-back)
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * Register Definitions
 * ============================================================================ */

/* DMAC Global Registers */
#define DMAC_IDREG              0x000       /* ID Register */
#define DMAC_IDREG2             0x008       /* ID Register 2 */
#define DMAC_ENABLE             0x010       /* DMAC Enable Register */
#define DMAC_IntStatus          0x018       /* Interrupt Status Register */
#define DMAC_RawIntStatus       0x020       /* Raw Interrupt Status Register */
#define DMAC_IntClear           0x028       /* Interrupt Clear Register */
#define DMAC_IntSignal_Enable   0x030       /* Interrupt Signal Enable Register */
#define DMAC_IntStatus_Enable   0x038       /* Interrupt Status Enable Register */
#define DMAC_IntType            0x040       /* Interrupt Type Register */
#define DMAC_ChIntStatus        0x048       /* Channel Interrupt Status Register */
#define DMAC_ChIntSignal_Enable 0x050       /* Channel Interrupt Signal Enable */
#define DMAC_ChIntStatus_Enable 0x058       /* Channel Interrupt Status Enable */
#define DMAC_ChIntType          0x060       /* Channel Interrupt Type */
#define DMAC_ChSuspend          0x068       /* Channel Suspend Register */
#define DMAC_ChSuspendAck       0x070       /* Channel Suspend Acknowledge */
#define DMAC_ChEnReg            0x078       /* Channel Enable Register */
#define DMAC_ChEnReg_W1S        0x080       /* Channel Enable Set Register */
#define DMAC_ChEnReg_W1C        0x088       /* Channel Enable Clear Register */
#define DMAC_CfgReg             0x090       /* Configuration Register */
#define DMAC_SyncPolReg         0x098       /* Synchronization Polarity */
#define DMAC_SyncTypeReg        0x0A0       /* Synchronization Type */
#define DMAC_ChPrior            0x0A8       /* Channel Priority */

/* Channel Register Block (per channel, offset = 0x100 * (channel-1)) */
#define CHAN_OFFSET(chan)       ((chan) * 0x100)

/* Channel Registers */
#define CHx_SAR                 0x000       /* Source Address Register */
#define CHx_DAR                 0x008       /* Destination Address Register */
#define CHx_BLOCK_TS            0x010       /* Block Transfer Size Register */
#define CHx_CTL                 0x018       /* Control Register */
#define CHx_CFG                 0x020       /* Configuration Register */
#define CHx_CFG2                0x028       /* Configuration Register 2 */
#define CHx_LLP                 0x028       /* Linked List Pointer Register */
#define CHx_LLP_STATUS          0x030       /* Linked List Pointer Status */
#define CHx_SWHSSRCREG          0x038       /* Software Handshake Source */
#define CHx_SWHSDSTREG          0x040       /* Software Handshake Destination */
#define CHx_BLK_TFR_RESUMEREQREG 0x048      /* Block Transfer Resume Request */
#define CHx_AXI_IDREG           0x050       /* AXI ID Register */
#define CHx_AXI_QOSREG          0x058       /* AXI QOS Register */
#define CHx_SSTAT               0x060       /* Source Status Register */
#define CHx_DSTAT               0x068       /* Destination Status Register */
#define CHx_SSTATAR             0x070       /* Source Status Fetch Address */
#define CHx_DSTATAR             0x078       /* Destination Status Fetch Address */
#define CHx_INTSTATUS_ENREG     0x080       /* Interrupt Status Enable */
#define CHx_INTSIGNAL_ENREG     0x088       /* Interrupt Signal Enable */
#define CHx_CLR_TFR             0x090       /* Clear Transfer Interrupt */
#define CHx_CLR_BLOCK           0x098       /* Clear Block Interrupt */
#define CHx_CLR_SRC_TRAN        0x0A0       /* Clear Source Transaction */
#define CHx_CLR_DST_TRAN        0x0A8       /* Clear Destination Transaction */
#define CHx_CLR_ERR             0x0B0       /* Clear Error Interrupt */
#define CHx_STATUSREG           0x0C0       /* Channel Status Register */

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/*
 * LLI Descriptor Structure (64 bytes, must be 64-byte aligned)
 * This structure matches the hardware expectation for LLI fetch
 */
typedef struct __attribute__((packed, aligned(64))) {
    uint64_t sar;           /* 0x00: Source Address */
    uint64_t dar;           /* 0x08: Destination Address */
    uint32_t block_ts;      /* 0x10: Block Transfer Size (number of transfers - 1) */
    uint32_t reserved1;     /* 0x14: Reserved */
    uint64_t llp;           /* 0x18: Next LLI Pointer (bits 63:6, lower 6 bits = 0) */
    uint64_t ctl;           /* 0x20: Control Register */
    uint64_t sstat;         /* 0x28: Source Status (for write-back) */
    uint64_t dstat;         /* 0x30: Destination Status (for write-back) */
    uint64_t llp_status;    /* 0x38: LLI Status (for write-back) */
} dw_dmac_lli_t;

/*
 * Transfer Configuration
 */
typedef struct {
    uint32_t channel;           /* DMA Channel number (1-based) */
    uint32_t total_size;        /* Total transfer size in bytes */
    uint32_t block_size;        /* Block size in bytes (for multi-block) */
    uint8_t  tr_width;          /* Transfer width: 0=1byte, 1=2bytes, 2=4bytes, 3=8bytes */
    uint8_t  src_msize;         /* Source burst length */
    uint8_t  dst_msize;         /* Destination burst length */
    bool     llp_writeback;     /* Enable LLI write-back */
} dw_dmac_config_t;

/* ============================================================================
 * Register Access Macros
 * ============================================================================ */

/* Base address should be defined by the platform */
#ifndef DMAC_BASE_ADDR
#define DMAC_BASE_ADDR  0x00000000  /* Platform specific */
#endif

#define DMAC_REG(offset)        (*(volatile uint32_t *)(DMAC_BASE_ADDR + (offset)))
#define DMAC_REG64(offset)      (*(volatile uint64_t *)(DMAC_BASE_ADDR + (offset)))

/* Channel register access */
#define CHAN_REG(chan, offset)  (*(volatile uint32_t *)(DMAC_BASE_ADDR + CHAN_OFFSET(chan) + (offset)))
#define CHAN_REG64(chan, offset) (*(volatile uint64_t *)(DMAC_BASE_ADDR + CHAN_OFFSET(chan) + (offset)))

/* ============================================================================
 * Bit Field Definitions
 * ============================================================================ */

/* DMAC_Enable Register */
#define DMAC_EN_BIT             (1 << 0)

/* Channel Enable Register */
#define CH_EN_BIT(chan)         (1 << ((chan) - 1))

/* CHx_CTL Register Fields */
/* Bits [2:0]: SRC_TR_WIDTH - Source Transfer Width */
#define CTL_SRC_TR_WIDTH_SHIFT  0
#define CTL_SRC_TR_WIDTH_MASK   (0x7 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_8BIT   (0 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_16BIT  (1 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_32BIT  (2 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_64BIT  (3 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_128BIT (4 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_256BIT (5 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_512BIT (6 << CTL_SRC_TR_WIDTH_SHIFT)
#define CTL_SRC_TR_WIDTH_1024BIT (7 << CTL_SRC_TR_WIDTH_SHIFT)

/* Bits [5:3]: DST_TR_WIDTH - Destination Transfer Width */
#define CTL_DST_TR_WIDTH_SHIFT  3
#define CTL_DST_TR_WIDTH_MASK   (0x7 << CTL_DST_TR_WIDTH_SHIFT)
#define CTL_DST_TR_WIDTH_8BIT   (0 << CTL_DST_TR_WIDTH_SHIFT)
#define CTL_DST_TR_WIDTH_16BIT  (1 << CTL_DST_TR_WIDTH_SHIFT)
#define CTL_DST_TR_WIDTH_32BIT  (2 << CTL_DST_TR_WIDTH_SHIFT)
#define CTL_DST_TR_WIDTH_64BIT  (3 << CTL_DST_TR_WIDTH_SHIFT)

/* Bits [10:8]: SRC_MSIZE - Source Burst Length */
#define CTL_SRC_MSIZE_SHIFT     8
#define CTL_SRC_MSIZE_MASK      (0x7 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_1         (0 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_4         (1 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_8         (2 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_16        (3 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_32        (4 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_64        (5 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_128       (6 << CTL_SRC_MSIZE_SHIFT)
#define CTL_SRC_MSIZE_256       (7 << CTL_SRC_MSIZE_SHIFT)

/* Bits [14:11]: DST_MSIZE - Destination Burst Length */
#define CTL_DST_MSIZE_SHIFT     11
#define CTL_DST_MSIZE_MASK      (0xF << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_1         (0 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_4         (1 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_8         (2 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_16        (3 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_32        (4 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_64        (5 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_128       (6 << CTL_DST_MSIZE_SHIFT)
#define CTL_DST_MSIZE_256       (7 << CTL_DST_MSIZE_SHIFT)

/* Bits [17:14]: ARLEN - AXI Read Burst Length */
#define CTL_ARLEN_SHIFT         14
#define CTL_ARLEN_MASK          (0xF << CTL_ARLEN_SHIFT)

/* Bits [21:18]: AWLEN - AXI Write Burst Length */
#define CTL_AWLEN_SHIFT         18
#define CTL_AWLEN_MASK          (0xF << CTL_AWLEN_SHIFT)

/* Bit 22: SINC - Source Address Increment */
#define CTL_SINC_BIT            (1 << 22)
#define CTL_SINC_INCREMENT      (1 << 22)
#define CTL_SINC_NOCHANGE       (0 << 22)

/* Bit 24: DINC - Destination Address Increment */
#define CTL_DINC_BIT            (1 << 24)
#define CTL_DINC_INCREMENT      (1 << 24)
#define CTL_DINC_NOCHANGE       (0 << 24)

/* Bit 27: SRC_STAT_EN - Source Status Fetch Enable */
#define CTL_SRC_STAT_EN_BIT     (1 << 27)

/* Bit 28: DST_STAT_EN - Destination Status Fetch Enable */
#define CTL_DST_STAT_EN_BIT     (1 << 28)

/* Bit 30: IOC_BlkTfr - Interrupt on Block Transfer Completion */
#define CTL_IOC_BLKTFR_BIT      (1 << 30)

/* Bit 31: SHADOWREG_OR_LLI_VALID - LLI Valid Indicator */
#define CTL_LLI_VALID_BIT       (1ULL << 31)

/* Bit 32: SHADOWREG_OR_LLI_LAST - Last LLI Indicator */
#define CTL_LLI_LAST_BIT        (1ULL << 32)

/* Bits [34:32]: SMS - Source Master Select */
#define CTL_SMS_SHIFT           32
#define CTL_SMS_MASK            (0x3 << CTL_SMS_SHIFT)
#define CTL_SMS_MASTER0         (0 << CTL_SMS_SHIFT)
#define CTL_SMS_MASTER1         (1 << CTL_SMS_SHIFT)

/* Bits [37:35]: DMS - Destination Master Select */
#define CTL_DMS_SHIFT           35
#define CTL_DMS_MASK            (0x3 << CTL_DMS_SHIFT)
#define CTL_DMS_MASTER0         (0 << CTL_DMS_SHIFT)
#define CTL_DMS_MASTER1         (1 << CTL_DMS_SHIFT)

/* CHx_CFG Register Fields */
/* Bits [1:0]: SRC_MULTBLK_TYPE - Source Multi-Block Type */
#define CFG_SRC_MLTBLK_TYPE_SHIFT   0
#define CFG_SRC_MLTBLK_TYPE_MASK    (0x3 << CFG_SRC_MLTBLK_TYPE_SHIFT)
#define CFG_MLTBLK_CONTIGUOUS       (0 << CFG_SRC_MLTBLK_TYPE_SHIFT)
#define CFG_MLTBLK_RELOAD           (1 << CFG_SRC_MLTBLK_TYPE_SHIFT)
#define CFG_MLTBLK_SHADOW_REG       (2 << CFG_SRC_MLTBLK_TYPE_SHIFT)
#define CFG_MLTBLK_LINKED_LIST      (3 << CFG_SRC_MLTBLK_TYPE_SHIFT)  /* 0b11 for LLI mode */

/* Bits [3:2]: DST_MULTBLK_TYPE - Destination Multi-Block Type */
#define CFG_DST_MLTBLK_TYPE_SHIFT   2
#define CFG_DST_MLTBLK_TYPE_MASK    (0x3 << CFG_DST_MLTBLK_TYPE_SHIFT)

/* Bits [11:8]: TT_FC - Transfer Type and Flow Control */
#define CFG_TT_FC_SHIFT         8
#define CFG_TT_FC_MASK          (0xF << CFG_TT_FC_SHIFT)
#define CFG_TT_FC_MEM2MEM       (0 << CFG_TT_FC_SHIFT)  /* Memory to Memory */
#define CFG_TT_FC_MEM2PER       (1 << CFG_TT_FC_SHIFT)  /* Memory to Peripheral */
#define CFG_TT_FC_PER2MEM       (2 << CFG_TT_FC_SHIFT)  /* Peripheral to Memory */
#define CFG_TT_FC_PER2PER       (3 << CFG_TT_FC_SHIFT)  /* Peripheral to Peripheral */

/* Bits [31:4]: Reserved */

/* DMAC_CfgReg Fields */
#define DMAC_CFG_INT_EN_BIT     (1 << 0)    /* Global Interrupt Enable */
#define DMAC_CFG_ERR_INT_EN_BIT (1 << 1)    /* Error Interrupt Enable */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Calculate number of blocks needed for a transfer
 */
static inline uint32_t calc_num_blocks(uint32_t total_size, uint32_t block_size)
{
    return (total_size + block_size - 1) / block_size;
}

/**
 * Convert transfer width to CTL field value
 */
static inline uint32_t tr_width_to_ctl(uint8_t tr_width)
{
    switch (tr_width) {
        case 0: return CTL_SRC_TR_WIDTH_8BIT;   /* 1 byte */
        case 1: return CTL_SRC_TR_WIDTH_16BIT;  /* 2 bytes */
        case 2: return CTL_SRC_TR_WIDTH_32BIT;  /* 4 bytes */
        case 3: return CTL_SRC_TR_WIDTH_64BIT;  /* 8 bytes */
        default: return CTL_SRC_TR_WIDTH_64BIT;
    }
}

/**
 * Get bytes per transfer from width encoding
 */
static inline uint32_t bytes_per_transfer(uint8_t tr_width)
{
    return (1U << tr_width);
}

/**
 * Wait for channel to be disabled (transfer complete)
 */
static int dmac_wait_channel_disabled(uint32_t channel, uint32_t timeout_ms)
{
    /* Platform-specific timeout implementation */
    uint32_t timeout = timeout_ms * 1000;  /* Convert to loop count approximation */

    while (timeout--) {
        if (!(DMAC_REG(DMAC_ChEnReg) & CH_EN_BIT(channel))) {
            return 0;  /* Success */
        }
        /* Small delay - platform specific */
        __asm__ volatile("nop");
    }

    return -1;  /* Timeout */
}

/**
 * Clear all interrupts for a channel
 */
static void dmac_clear_channel_interrupts(uint32_t channel)
{
    /* Clear all interrupt types */
    CHAN_REG(channel, CHx_CLR_TFR) = 0x1;
    CHAN_REG(channel, CHx_CLR_BLOCK) = 0x1;
    CHAN_REG(channel, CHx_CLR_SRC_TRAN) = 0x1;
    CHAN_REG(channel, CHx_CLR_DST_TRAN) = 0x1;
    CHAN_REG(channel, CHx_CLR_ERR) = 0x1;
}

/**
 * Initialize DMAC controller
 */
int dmac_init(void)
{
    /* Enable DMAC */
    DMAC_REG(DMAC_ENABLE) |= DMAC_EN_BIT;

    /* Enable global interrupts (if using interrupts) */
    DMAC_REG(DMAC_CfgReg) |= DMAC_CFG_INT_EN_BIT;

    /* Wait for DMAC to be ready */
    while (!(DMAC_REG(DMAC_ENABLE) & DMAC_EN_BIT));

    return 0;
}

/**
 * Deinitialize DMAC controller
 */
void dmac_deinit(void)
{
    /* Disable all channels */
    DMAC_REG(DMAC_ChEnReg_W1C) = 0xFFFFFFFF;

    /* Disable DMAC */
    DMAC_REG(DMAC_ENABLE) &= ~DMAC_EN_BIT;
}

/* ============================================================================
 * LLI Mode Memory-to-Memory Transfer Implementation
 * ============================================================================ */

/**
 * Build LLI chain for memory-to-memory transfer
 *
 * @param lli_array     Array of LLI descriptors (must be 64-byte aligned)
 * @param src_addr      Source buffer address
 * @param dst_addr      Destination buffer address
 * @param total_bytes   Total number of bytes to transfer
 * @param block_bytes   Block size in bytes (0 = single block)
 * @param tr_width      Transfer width (0=1B, 1=2B, 2=4B, 3=8B)
 * @return              Number of LLIs created, or -1 on error
 */
int dmac_build_lli_chain(
    dw_dmac_lli_t *lli_array,
    uint64_t src_addr,
    uint64_t dst_addr,
    uint32_t total_bytes,
    uint32_t block_bytes,
    uint8_t tr_width)
{
    uint32_t num_blocks;
    uint32_t bytes_per_trans;
    uint32_t i;

    if (!lli_array || total_bytes == 0) {
        return -1;
    }

    /* Calculate bytes per transfer based on width */
    bytes_per_trans = bytes_per_transfer(tr_width);

    /* Calculate number of blocks needed */
    if (block_bytes == 0 || block_bytes > total_bytes) {
        /* Single block transfer */
        num_blocks = 1;
        block_bytes = total_bytes;
    } else {
        /* Multi-block transfer */
        num_blocks = calc_num_blocks(total_bytes, block_bytes);
    }

    /* Build LLI chain */
    for (i = 0; i < num_blocks; i++) {
        uint32_t remaining_bytes = total_bytes - (i * block_bytes);
        uint32_t current_block_size = (remaining_bytes < block_bytes) ?
                                       remaining_bytes : block_bytes;
        uint32_t num_transfers = (current_block_size / bytes_per_trans) - 1;

        /* Source Address */
        lli_array[i].sar = src_addr + (i * block_bytes);

        /* Destination Address */
        lli_array[i].dar = dst_addr + (i * block_bytes);

        /* Block Transfer Size (BLOCK_TS = number of transfers - 1) */
        lli_array[i].block_ts = num_transfers;

        /* Next LLI Pointer */
        if (i < num_blocks - 1) {
            /* Not the last LLI - point to next */
            lli_array[i].llp = ((uint64_t)&lli_array[i + 1]) & ~0x3FULL;  /* Clear lower 6 bits */
        } else {
            /* Last LLI - null pointer, set LAST bit */
            lli_array[i].llp = 0;
        }

        /* Control Register */
        lli_array[i].ctl =
            tr_width_to_ctl(tr_width) |                    /* SRC_TR_WIDTH */
            tr_width_to_ctl(tr_width) |                    /* DST_TR_WIDTH */
            CTL_SINC_INCREMENT |                           /* Increment source address */
            CTL_DINC_INCREMENT |                           /* Increment destination address */
            CTL_SRC_MSIZE_16 |                             /* Source burst: 16 beats */
            CTL_DST_MSIZE_16 |                             /* Destination burst: 16 beats */
            CTL_SMS_MASTER1 |                              /* Use AXI Master 1 for read */
            CTL_DMS_MASTER1 |                              /* Use AXI Master 1 for write */
            CTL_IOC_BLKTFR_BIT;                            /* Interrupt on block complete */

        /* Set LLI_VALID and LLI_LAST flags */
        if (i == num_blocks - 1) {
            /* Last LLI */
            lli_array[i].ctl |= CTL_LLI_VALID_BIT | CTL_LLI_LAST_BIT;
        } else {
            /* Not last LLI */
            lli_array[i].ctl |= CTL_LLI_VALID_BIT;
        }

        /* Status fields (for write-back) */
        lli_array[i].sstat = 0;
        lli_array[i].dstat = 0;
        lli_array[i].llp_status = 0;
    }

    return (int)num_blocks;
}

/**
 * Start LLI-based memory-to-memory transfer
 *
 * Programming Flow (from DW_axi_dmac Databook Section 7.2):
 * 1. Read DMAC_ChEnReg to select available channel
 * 2. Program CHx_CFG with SRC_MLTBLK_TYPE = DST_MLTBLK_TYPE = 2'b11 (Linked List)
 * 3. Program CHx_LLP with base address of first LLI
 * 4. Create LLI descriptors in system memory
 * 5. Enable channel via DMAC_ChEnReg
 * 6. Hardware fetches LLI and starts transfer
 * 7. Wait for completion
 *
 * @param channel       DMA channel number (1-based)
 * @param lli_base      Base address of first LLI descriptor (must be 64-byte aligned)
 * @param use_interrupt Enable transfer complete interrupt
 * @return              0 on success, -1 on error
 */
int dmac_start_lli_transfer(
    uint32_t channel,
    uint64_t lli_base,
    bool use_interrupt)
{
    uint32_t cfg_val;
    uint32_t ctl_val;

    /* Validate channel */
    if (channel == 0) {
        return -1;
    }

    /* Validate LLI alignment (must be 64-byte aligned) */
    if (lli_base & 0x3F) {
        return -1;
    }

    /* Step 1: Ensure channel is disabled */
    DMAC_REG(DMAC_ChEnReg_W1C) = CH_EN_BIT(channel);

    /* Wait for channel to disable */
    while (DMAC_REG(DMAC_ChEnReg) & CH_EN_BIT(channel));

    /* Clear any pending interrupts */
    dmac_clear_channel_interrupts(channel);

    /* Step 2: Program CHx_CFG register
     * - SRC_MULTBLK_TYPE = 2'b11 (Linked List)
     * - DST_MULTBLK_TYPE = 2'b11 (Linked List)
     * - TT_FC = 0x0 (Memory to Memory)
     */
    cfg_val = CFG_MLTBLK_LINKED_LIST |                    /* SRC_MULTBLK_TYPE = 11 */
              (CFG_MLTBLK_LINKED_LIST << 2) |             /* DST_MULTBLK_TYPE = 11 */
              CFG_TT_FC_MEM2MEM;                          /* Memory to Memory */

    CHAN_REG64(channel, CHx_CFG) = cfg_val;

    /* Step 3: Program CHx_LLP with base address of first LLI
     * Only bits [63:6] are used, lower 6 bits are always 0
     */
    CHAN_REG64(channel, CHx_LLP) = lli_base & ~0x3FULL;

    /* Step 4: LLIs are already created in system memory by dmac_build_lli_chain() */

    /* Step 5: Enable the channel */
    DMAC_REG(DMAC_ChEnReg_W1S) = CH_EN_BIT(channel);

    /* Optional: Configure interrupts */
    if (use_interrupt) {
        /* Enable block transfer complete interrupt */
        CHAN_REG(channel, CHx_INTSIGNAL_ENREG) |= (1 << 1);  /* Block interrupt */
        CHAN_REG(channel, CHx_INTSTATUS_ENREG) |= (1 << 1);
    }

    /* Step 6: Hardware automatically fetches first LLI and starts transfer */

    return 0;
}

/**
 * Wait for LLI transfer to complete
 *
 * @param channel       DMA channel number
 * @param timeout_ms    Timeout in milliseconds
 * @return              0 on success, -1 on timeout
 */
int dmac_wait_lli_transfer_complete(uint32_t channel, uint32_t timeout_ms)
{
    /* Wait for channel to be disabled (transfer complete) */
    if (dmac_wait_channel_disabled(channel, timeout_ms) != 0) {
        return -1;  /* Timeout */
    }

    /* Check for errors */
    if (DMAC_REG(DMAC_RawIntStatus) & (1 << ((channel - 1) + 8))) {
        /* Error interrupt pending */
        return -2;  /* Error */
    }

    return 0;  /* Success */
}

/**
 * Complete LLI transfer (cleanup after completion)
 *
 * @param channel       DMA channel number
 */
void dmac_complete_lli_transfer(uint32_t channel)
{
    /* Disable channel */
    DMAC_REG(DMAC_ChEnReg_W1C) = CH_EN_BIT(channel);

    /* Clear all interrupts */
    dmac_clear_channel_interrupts(channel);
}

/* ============================================================================
 * High-Level API: Simple Memory-to-Memory Copy using LLI
 * ============================================================================ */

/**
 * Perform memory-to-memory copy using DMA LLI mode
 *
 * This is the main API function for LLI-based DMA copy operations.
 * It handles the entire flow: building LLI chain, starting transfer, and waiting for completion.
 *
 * @param src           Source buffer address
 * @param dst           Destination buffer address
 * @param size          Number of bytes to copy
 * @param block_size    Block size for each LLI (0 = auto, use single block if possible)
 * @param channel       DMA channel to use (0 = auto-select)
 * @return              0 on success, negative on error
 *
 * Example usage:
 *   // Copy 1MB using 4KB blocks on channel 1
 *   uint8_t *src = (uint8_t *)0x10000000;
 *   uint8_t *dst = (uint8_t *)0x20000000;
 *   dmac_lli_memcpy(dst, src, 1024*1024, 4096, 1);
 */
int dmac_lli_memcpy(void *dst, const void *src, uint32_t size, uint32_t block_size, uint32_t channel)
{
    dw_dmac_lli_t lli_descriptors[256];  /* Support up to 256 blocks */
    uint32_t num_blocks;
    int ret;

    /* Validate parameters */
    if (!dst || !src || size == 0) {
        return -1;
    }

    /* Check alignment requirements */
    if (((uint64_t)src & 0x3) || ((uint64_t)dst & 0x3)) {
        /* Not 4-byte aligned - may need byte transfers */
    }

    /* Ensure LLI array is 64-byte aligned */
    uint64_t lli_aligned = ((uint64_t)lli_descriptors + 63) & ~0x3FULL;
    dw_dmac_lli_t *lli_ptr = (dw_dmac_lli_t *)lli_aligned;

    /* Auto-select channel if not specified */
    if (channel == 0) {
        /* Find first available channel */
        for (channel = 1; channel <= 8; channel++) {
            if (!(DMAC_REG(DMAC_ChEnReg) & CH_EN_BIT(channel))) {
                break;
            }
        }
        if (channel > 8) {
            return -3;  /* No available channel */
        }
    }

    /* Calculate required number of blocks */
    if (block_size == 0) {
        block_size = size;  /* Single block */
    }
    num_blocks = calc_num_blocks(size, block_size);

    if (num_blocks > 256) {
        return -4;  /* Too many blocks */
    }

    /* Build LLI chain for 64-bit transfers */
    ret = dmac_build_lli_chain(lli_ptr,
                                (uint64_t)src,
                                (uint64_t)dst,
                                size,
                                block_size,
                                3);  /* tr_width = 3 (8 bytes) */
    if (ret < 0) {
        return ret;
    }

    /* Initialize DMAC if not already done */
    if (!(DMAC_REG(DMAC_ENABLE) & DMAC_EN_BIT)) {
        dmac_init();
    }

    /* Start LLI transfer */
    ret = dmac_start_lli_transfer(channel, (uint64_t)lli_ptr, false);
    if (ret != 0) {
        return ret;
    }

    /* Wait for completion */
    ret = dmac_wait_lli_transfer_complete(channel, 1000);  /* 1 second timeout */

    /* Cleanup */
    dmac_complete_lli_transfer(channel);

    return ret;
}

/* ============================================================================
 * Interrupt Handler (to be called from platform interrupt handler)
 * ============================================================================ */

/**
 * DMA Channel Interrupt Handler
 *
 * Call this from your platform's interrupt handler when a DMA interrupt occurs.
 *
 * @param channel       DMA channel number
 * @return              true if interrupt was handled
 */
bool dmac_channel_interrupt_handler(uint32_t channel)
{
    uint32_t int_status;

    /* Check if this channel has pending interrupts */
    int_status = DMAC_REG(DMAC_RawIntStatus);

    if (int_status & (1 << (channel - 1))) {
        /* Block transfer complete interrupt */

        /* Clear the interrupt */
        CHAN_REG(channel, CHx_CLR_BLOCK) = 0x1;

        return true;
    }

    if (int_status & (1 << ((channel - 1) + 8))) {
        /* Error interrupt */

        /* Clear the error */
        CHAN_REG(channel, CHx_CLR_ERR) = 0x1;

        return true;
    }

    return false;
}

/* ============================================================================
 * Example Usage
 * ============================================================================ */

#if 0  /* Example code - not compiled */

/* Example 1: Simple large memory copy using LLI mode */
void example_large_memcpy(void)
{
    uint8_t *source = (uint8_t *)0x10000000;
    uint8_t *destination = (uint8_t *)0x20000000;
    uint32_t size = 10 * 1024 * 1024;  /* 10 MB */
    uint32_t block_size = 4096;         /* 4 KB per block */

    /* Perform DMA copy using LLI mode */
    int result = dmac_lli_memcpy(destination, source, size, block_size, 1);

    if (result == 0) {
        /* Transfer successful */
    } else {
        /* Handle error */
    }
}

/* Example 2: Manual LLI chain construction for complex transfers */
void example_manual_lli(void)
{
    /* Allocate LLI descriptors (must be 64-byte aligned) */
    dw_dmac_lli_t lli_chain[4] __attribute__((aligned(64)));

    /* Build custom LLI chain */
    dmac_build_lli_chain(lli_chain,
                         0x10000000,  /* Source */
                         0x20000000,  /* Destination */
                         65536,       /* 64KB total */
                         16384,       /* 16KB per block */
                         3);          /* 8-byte transfers */

    /* Initialize DMAC */
    dmac_init();

    /* Start transfer on channel 2 */
    dmac_start_lli_transfer(2, (uint64_t)lli_chain, true);

    /* Wait with timeout */
    if (dmac_wait_lli_transfer_complete(2, 5000) == 0) {
        /* Success */
    }

    /* Cleanup */
    dmac_complete_lli_transfer(2);
}

/* Example 3: Using interrupts for transfer completion notification */
volatile bool g_dma_transfer_complete = false;

void DMA_Channel1_IRQHandler(void)
{
    if (dmac_channel_interrupt_handler(1)) {
        g_dma_transfer_complete = true;
    }
}

void example_interrupt_driven(void)
{
    uint8_t *src = (uint8_t *)0x10000000;
    uint8_t *dst = (uint8_t *)0x20000000;

    g_dma_transfer_complete = false;

    /* Build and start transfer with interrupts enabled */
    dmac_lli_memcpy(dst, src, 1024*1024, 4096, 1);

    /* Wait for interrupt (platform-specific WFI) */
    while (!g_dma_transfer_complete) {
        __asm__ volatile("wfi");
    }
}

#endif  /* Example code */
