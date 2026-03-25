# pcie fw程序demo
```
#include <edge10_mcu.h>
#include <ARMv8MML_SP.h>
#include <address_map.h>
#include <ints_star.h>
#include <regs.h>
#include <stdio.h>

#include <string.h>
#include "common.h"
#include "types.h"
#include "platform.h"
#include "printf.h"
#include "address_map.h"
#include "regs.h"
#include "spi.h"
#include "spi_nor.h"
#include "pcie_main.h"
#include "timer.h"
#include "platform.h"


#define PCIE_TEXT_FIRST __attribute__((section(".pcie.text.first")))
#define PCIE_TEXT __attribute__((section(".pcie.text")))
#define PCIE_DATA __attribute__((section(".pcie.str")))

/* reserved byte for intellif id(sn/card_number) */
#define NOR_FLASH_ID_BASE_ADDR                                                 \
	0x7FFC0 //reserve 64byte at the end of nor flash (/dev/mtd0)
#define APSRAM_ID_BASE_ADDR 0x6007FA84 //reserve 52byte (7FA84~7FAB7)
#define READ_SIZE_NOR_TO_APSRAM 32 //(SN(20) + card(4) + reserved)
#define APSRAM_PCIEFW_VER_BASE_ADDR 0x6007FA20

/* Torrent PHY registers */
#define REG_CMN_PLL0_VCOCAL_TCTRL_ADDR                                         \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x82)
#define REG_CMN_PLL1_VCOCAL_TCTRL_ADDR                                         \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0xC2)
#define REG_CMN_PDIAG_PLL0_CP_PADJ_M0                                          \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1A4)
#define REG_CMN_PDIAG_PLL0_FILT_PADJ_M0                                        \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1A6)
#define REG_CMN_PLL0_LOCK_REFCNT_START_ADDR                                    \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x9C)
#define REG_CMN_PLL0_LOCK_PLLCNT_START_ADDR                                    \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x9E)
#define REG_CMN_PDIAG_PLL0_CP_PADJ_M1_ADDR                                     \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1B4)
#define REG_CMN_PDIAG_PLL0_FILT_PADJ_M1_ADDR                                   \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1B6)
#define REG_CMN_PDIAG_PLL1_CP_PADJ_M0_ADDR                                     \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1C4)
#define REG_CMN_PDIAG_PLL1_FILT_PADJ_M0_ADDR                                   \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1C6)
#define REG_CMN_DIAG_BIAS_OVRD1_ADDR                                           \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x1E1)
#define REG_CMN_RXCAL_TUNE_ADDR (HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x113)
#define REG_CMN_TXPUCAL_TUNE_ADDR                                              \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x103)
#define REG_CMN_TXPDCAL_TUNE_ADDR                                              \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * 0x10B)
#define REG_RX_REE_TAP1_CLIP_ADDR(n)                                           \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * (0x8171 + (n << 9)))
#define REG_RX_REE_TAP2TON_CLIP_ADDR(n)                                        \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * (0x8172 + (n << 9)))
#define REG_RX_DIAG_ACYA_ADDR(n)                                               \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * (0x81FF + (n << 9)))
#define REG_RX_DIAG_DFE_AMP_TUNE_ADDR(n)                                       \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * (0x81E1 + (n << 9)))
#define REG_RX_SAMP_DAC_CTRL_ADDR(n)                                           \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * (0x8058 + (n << 9)))
#define REG_RX_DIAG_LFEQ_TUNE_ADDR(n)                                          \
	(HSIO_S4_TORRENT_PHY__REGISTER_BASE + 4 * (0x81E6 + (n << 9)))

/* PCIe core registers */
#define PCIE_LM_BASE (HSIO_S4_PCIE_CONTROLLER_X8_REGISTER_BASE + 0x100000)
#define PCIE_AXI_CFG_BASE (HSIO_S4_PCIE_CONTROLLER_X8_REGISTER_BASE + 0x400000)
#define REG_DEBUG_MUX_CTRL_ADDR (PCIE_LM_BASE + 0x208)
#define REG_DEBUG_MUX_CTRL2_ADDR (PCIE_LM_BASE + 0x234)

#define X6000_PROD_MAGIC_ADDR 0x6007FAB8
#define X6000_LM_MAGIC 0x4C4D4C4D
#define X6000_DS_MAGIC 0x44534453
#define EXT_INIT_BACKUP_ADDR 0x6007FABC
#define EFUSE_BLOCK81_ADDR 0xa7400544
#define GEN3X8_VAL 0x32400011
#define GEN2X2_VAL 0x11400011

#if defined(IPU_X2000)
char version[] PCIE_DATA = "IPU-X2000 PCIe Firmware V1.0.7\r\n";
#elif defined(IPU_X5000)
char version[] PCIE_DATA = "IPU-X5000 PCIe Firmware V1.0.6\r\n";
#elif defined(IPU_X6000)
char version[] PCIE_DATA = "IPU-X6000 PCIe Firmware V1.0.1\r\n";
#elif defined(IPU_X6000_VS)
char version[] PCIE_DATA = "IPU-X6000-VS PCIe Firmware V0.0.6\r\n";
#elif defined(IPU_X6000_LM)
char version[] PCIE_DATA = "IPU-X6000-LM PCIe Firmware V0.0.3\r\n";
#elif defined(IPU_X6000_DS)
char version[] PCIE_DATA = "IPU-X6000-DS PCIe Firmware V0.0.5\r\n";
#endif
char resetsrc[] PCIE_DATA = "Lastword reset source: 0x%08x\r\n";
char phyclknstb[] PCIE_DATA = "PHY clock not stable!\r\n";

int PCIE_TEXT pcie_ep_init_ext(void)
{
	u32 val;
	u32 hot_reset_flag = 0;
#ifdef IS_ASIC
	int n;
#endif
	u32 group_id = (read_mreg32(EFUSE_BLOCK81_ADDR) & 0x2) >> 1;

	//val = read_mreg32(IOMUX_AP_PWM1_ADDR);
	/* pull down */
	//write_mreg32(IOMUX_AP_PWM1_ADDR, (val &
	//	(~(IOMUX_PINREG_WPD_U_IOPAD_AP_PWM1_WPD_MASK |
	//	IOMUX_PINREG_WPU_U_IOPAD_AP_PWM1_WPU_MASK))) |
	//	(0x2 << IOMUX_PINREG_WPU_U_IOPAD_AP_PWM1_WPU_POS));

	write_mreg32(PMU_HSIO_SYS_PWR_CONTROL_ADDR + PMU_APB_REG_SET_OFFSET,
		     PMU_PORSTN_HSIO_SYS_ASYNC_MASK);
	usleep(20);
	write_mreg32(PMU_HSIO_SYS_PWR_CONTROL_ADDR + PMU_APB_REG_SET_OFFSET,
		     PMU_PD_CGM_EN_HSIO_SYS_ASYNC_MASK);
	usleep(10);

	write_mreg32(HSIO_GEN_SEL_ADDR, HSIO_GEN_SEL_CLK_HS_APB_MASK |
						HSIO_GEN_SEL_CLK_HS_AXI_MASK);

	/* check if this is a hot reset boot */
	hot_reset_flag = read_mreg32(AON_PCIE_HOT_RESET_STATUS_ADDR);
	if (!hot_reset_flag) { //cold only
		/* HSIO reset control register */
		val = read_mreg32(PCIE_RST_SWRST_ADDR);
		write_mreg32(PCIE_RST_SWRST_ADDR,
			     val & (~PCIE_RST_SWRST_SWRST_HSIO_SYS_CTRL_MASK));
		usleep(1);

#ifdef IS_ASIC
		/* HSIO system control register */
		write_mreg32(HSIO_TORRENT_PHY_REF_CTRL_ADDR,
			     0x1 << HSIO_CMN_REFCLK_INT_MODE_POS |
				     0x2 << HSIO_CMN_REFCLK_DIG_DIV_POS);
		write_mreg32(HSIO_TORRENT_PHY_MODE_SEL_ADDR, 0x0);
		write_mreg32(HSIO_TORRENT_PHY_CTRL1_ADDR, 0x0);
#endif
		write_mreg32(HSIO_PIPE_MUX_CFG_ADDR, 0x0 << HSIO_IPS_CFG_POS);

		/* HSIO clock control register, set/clear reg */
		val = read_mreg32(HSIO_HSIO_CLK_GATE_ADDR);
		write_mreg32((HSIO_HSIO_CLK_GATE_ADDR),
			     val | HSIO_CGM_EN_PCLK_PCIE0_MASK |
				     HSIO_CGM_EN_CLK_PCIE0_AXI_MASK |
				     HSIO_CGM_EN_CLK_PCIE0_PM_MASK |
				     HSIO_CGM_EN_PCLK_COMBO_PHY_MASK);
	}

	/* HSIO reset control register */
	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & (~HSIO_RST_SWRST_SWRST_PCIE0_APB_MASK));

	if (!hot_reset_flag) { //cold only
		val = read_mreg32(PCIE_RST_SWRST_ADDR);
		write_mreg32(PCIE_RST_SWRST_ADDR,
			     val & (~PCIE_RST_SWRST_SWRST_APB_COMB_PHY_MASK));
		usleep(1);

#ifdef IS_ASIC
		/* Torrent PHY register */
		write_mreg32(REG_CMN_PLL0_VCOCAL_TCTRL_ADDR, 0x3);
		write_mreg32(REG_CMN_PLL1_VCOCAL_TCTRL_ADDR, 0x3);
		write_mreg32(REG_CMN_PDIAG_PLL0_CP_PADJ_M0, 0x2D);
		write_mreg32(REG_CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x101);
		write_mreg32(REG_CMN_PLL0_LOCK_REFCNT_START_ADDR, 0xC7);
		write_mreg32(REG_CMN_PLL0_LOCK_PLLCNT_START_ADDR, 0xC7);
		write_mreg32(REG_CMN_PDIAG_PLL0_CP_PADJ_M1_ADDR, 0x3F);
		write_mreg32(REG_CMN_PDIAG_PLL0_FILT_PADJ_M1_ADDR, 0x101);
		write_mreg32(REG_CMN_PDIAG_PLL1_CP_PADJ_M0_ADDR, 0x3F);
		write_mreg32(REG_CMN_PDIAG_PLL1_FILT_PADJ_M0_ADDR, 0x101);
		write_mreg32(REG_CMN_DIAG_BIAS_OVRD1_ADDR, 0x7700);
		write_mreg32(REG_CMN_RXCAL_TUNE_ADDR, 0x3);
		write_mreg32(REG_CMN_TXPUCAL_TUNE_ADDR, 0xD);
		write_mreg32(REG_CMN_TXPDCAL_TUNE_ADDR, 0xD);

		for (n = 0; n < 8; n++) {
			write_mreg32(REG_RX_REE_TAP1_CLIP_ADDR(n), 0x19);
			write_mreg32(REG_RX_REE_TAP2TON_CLIP_ADDR(n), 0x19);
			write_mreg32(REG_RX_DIAG_ACYA_ADDR(n), 0x1);
			write_mreg32(REG_RX_DIAG_DFE_AMP_TUNE_ADDR(n), 0x4BDD);
			write_mreg32(REG_RX_SAMP_DAC_CTRL_ADDR(n), 0x10);
			write_mreg32(REG_RX_DIAG_LFEQ_TUNE_ADDR(n), 0xC4);
		}
#endif

		/* 1st stage customized firmware */

		/* HSIO reset control register */
		val = read_mreg32(PCIE_RST_SWRST_ADDR);
		write_mreg32(PCIE_RST_SWRST_ADDR,
			     val & (~PCIE_RST_SWRST_SWRST_COMB_PHY_MASK));
	}
	usleep(1);

	/* PCIe x8 system control register */
	if (hot_reset_flag) { //hot only
		val = read_mreg32(PCIEX8_CONTROLLER_MISC_CTRL_STS1_ADDR);
		write_mreg32(
			PCIEX8_CONTROLLER_MISC_CTRL_STS1_ADDR,
			val & ~PCIEX8_CONTROLLER_MISC_CTRL_STS1_LINK_TRAINING_ENABLE_MASK);
	}

	/* HSIO reset control register */
	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & (~HSIO_RST_SWRST_SWRST_COMB_PHY_PCIE0_MASK));
	usleep(1);

	if (group_id == 1) {
		do {
			val = read_mreg32(
				PCIEX8_CONTROLLER_MISC_CTRL_STS2_ADDR);
			if ((val &
			     PCIEX8_CONTROLLER_MISC_CTRL_STS2_PHY_CLK_STABLE_MASK) ==
			    0)
				break;
			usleep(10);
		} while (1);
	} else {
		u32 count = 200;
		do {
			val = read_mreg32(
				PCIEX8_CONTROLLER_MISC_CTRL_STS2_ADDR);
			if ((val &
			     PCIEX8_CONTROLLER_MISC_CTRL_STS2_PHY_CLK_STABLE_MASK) ==
			    0)
				break;
			usleep(10);
		} while (--count);
		if (count == 0) {
			eprintf(phyclknstb);
			return -1;
		}
	}

	if (!hot_reset_flag) { //cold only
		/* 2nd stage customized firmware */
#if defined(IPU_X2000)
		write_mreg32(0x5a200908, GEN2X2_VAL);
#elif (defined(IPU_X5000) || defined(IPU_X6000) || defined(IPU_X6000_VS) ||    \
       defined(IPU_X6000_LM) || defined(IPU_X6000_DS))
		write_mreg32(0x5a200908, GEN3X8_VAL);
#endif
	}

	/* HSIO reset control register */
	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & ~HSIO_RST_SWRST_SWRST_PCIE0_PIPE_MASK);
	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & ~HSIO_RST_SWRST_SWRST_PCIE0_LINK_MASK);
	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & ~HSIO_RST_SWRST_SWRST_PCIE0_AXI_MASK);

	if (!hot_reset_flag) { //cold only
		val = read_mreg32(PCIE_RST_SWRST_ADDR);
		write_mreg32(
			PCIE_RST_SWRST_ADDR,
			val & ~PCIE_RST_SWRST_SWRST_PCIE0_MGMT_STICKY_MASK);
	}

	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & ~HSIO_RST_SWRST_SWRST_PCIE0_MGMT_MASK);
	val = read_mreg32(HSIO_RST_SWRST_ADDR);
	write_mreg32(HSIO_RST_SWRST_ADDR,
		     val & ~HSIO_RST_SWRST_SWRST_PCIE0_PM_MASK);
	usleep(1);

	/* max np outstanding limit */
	val = read_mreg32(REG_DEBUG_MUX_CTRL2_ADDR);
#if defined(IPU_X2000)
	write_mreg32(REG_DEBUG_MUX_CTRL2_ADDR,
		     (val & (~(0x3FF << 13))) | (0x3 << 13) | (0x1 << 31));
#elif (defined(IPU_X6000_DS) || defined(IPU_X6000))
	write_mreg32(REG_DEBUG_MUX_CTRL2_ADDR,
		     (val & (~(0x3FF << 13))) | (0x4 << 13) | (0x1 << 31));
#else
	write_mreg32(REG_DEBUG_MUX_CTRL2_ADDR, val | (0x1 << 31));
#endif

	/* disable ordering check */
#if (defined(IPU_X6000_DS) || defined(IPU_X6000))
	val = read_mreg32(REG_DEBUG_MUX_CTRL_ADDR);
	write_mreg32(REG_DEBUG_MUX_CTRL_ADDR, val | (0x1 << 30));
#endif

	/* 3rd stage customized firmware */
	/* device id */
	write_mreg32(0x58000000, 0x200017cd);

	/* BAR configuration */
#if defined(IPU_X2000)
	/* bar0 16M 64bit non-pref + bar2 16M 64bit pref*/
	write_mreg32(0x58100240, 0x85f185d1);
#elif defined(IPU_X5000)
	/* bar0 16M 64bit non-pref + bar2 64M 64bit pref*/
	write_mreg32(0x58100240, 0x85f385d1);
#elif defined(IPU_X6000)
	if (group_id == 0) {
		/* bar0 16M 32bit non-pref + bar1 16M 32bit non-pref +
		bar2 64M 64bit pref + bar4 1G 64bit pref */
		write_mreg32(0x58100240, 0xf39191);
		write_mreg32(0x58100244, 0xf7);
	} else {
		/* bar0 16M 64bit non-pref + bar2 64M 64bit pref +
		bar4 4G 64bit pref */
		write_mreg32(0x58100240, 0xf300d1);
		write_mreg32(0x58100244, 0xf9);
	}
#elif defined(IPU_X6000_VS)
	if (group_id == 0) {
		/* bar0 16M 32bit non-pref + bar1 16M 32bit non-pref +
		   bar2 64M 64bit pref + bar4 64M 64bit pref */
		write_mreg32(0x58100240, 0xf39191);
		write_mreg32(0x58100244, 0xf3);
	} else {
		/* bar0 16M 64bit non-pref + bar2 64M 64bit pref */
		write_mreg32(0x58100240, 0xf300d1);
	}
#elif defined(IPU_X6000_LM)
	if (group_id == 0) {
		/* bar0 16M 32bit non-pref + bar1 16M 32bit non-pref +
		   bar2 16M 32bit pref + bar3 16M 32bit pref +
		   bar4 16M 64bit pref */
		write_mreg32(0x58100240, 0xb1b19191);
		write_mreg32(0x58100244, 0xf1);
	} else {
		/* bar0 16M 64bit non-pref + bar2 16M 64bit pref +
		   bar4 4G 64bit pref */
		write_mreg32(0x58100240, 0xf100d1);
		write_mreg32(0x58100244, 0xf9);
	}
#elif defined(IPU_X6000_DS)
	if (group_id == 0) {
		/* bar0 16M 32bit non-pref + bar1 16M 32bit non-pref +
		bar2 32M 64bit pref + bar4 1G 64bit pref */
		write_mreg32(0x58100240, 0xf29191);
		write_mreg32(0x58100244, 0xf7);
	} else {
		/* bar0 16M 64bit non-pref + bar2 16M 64bit pref +
		bar4 4G 64bit pref */
		write_mreg32(0x58100240, 0xf100d1);
		write_mreg32(0x58100244, 0xf9);
	}
#endif

	/* Link training enable */
	val = read_mreg32(PCIEX8_CONTROLLER_MISC_CTRL_STS1_ADDR);
	write_mreg32(
		PCIEX8_CONTROLLER_MISC_CTRL_STS1_ADDR,
		val | PCIEX8_CONTROLLER_MISC_CTRL_STS1_CONFIG_ENABLE_MASK |
			PCIEX8_CONTROLLER_MISC_CTRL_STS1_LINK_TRAINING_ENABLE_MASK);

	/* apt base address start from 0x6000_0000,
	   cos bootrom will set bar0 inbound atu later in pcie_main().
	   apt base address will be changed in pcs. */
#if defined(IPU_X6000)
	/* region 0 */
	write_mreg32(0x5a200200, 0x60000000);
	write_mreg32(0x5a200204, 0x0);
	write_mreg32(0x5a200208, 0x6007FFFF);
	write_mreg32(0x5a20020c, 0x0);
	write_mreg32(0x5a200210, 0x60000000);
	write_mreg32(0x5a200214, 0x0);

	/* region 1 */
	write_mreg32(0x5a200218, 0x60080000);
	write_mreg32(0x5a20021c, 0x0);
	write_mreg32(0x5a200220, 0x600FFFFF);
	write_mreg32(0x5a200224, 0x0);
	write_mreg32(0x5a200228, 0x60000000);
	write_mreg32(0x5a20022c, 0x4);

	/* region 2 */
	write_mreg32(0x5a200230, 0x60100000);
	write_mreg32(0x5a200234, 0x0);
	write_mreg32(0x5a200238, 0x6017FFFF);
	write_mreg32(0x5a20023c, 0x0);
	write_mreg32(0x5a200240, 0x60000000);
	write_mreg32(0x5a200244, 0x1C);

	/* region 3 */
	write_mreg32(0x5a200248, 0x60180000);
	write_mreg32(0x5a20024c, 0x0);
	write_mreg32(0x5a200250, 0x601FFFFF);
	write_mreg32(0x5a200254, 0x0);
	write_mreg32(0x5a200258, 0x60000000);
	write_mreg32(0x5a20025c, 0x20);

	/* region 4 */
	write_mreg32(0x5a200260, 0x60200000);
	write_mreg32(0x5a200264, 0x0);
	write_mreg32(0x5a200268, 0x60200FFF);
	write_mreg32(0x5a20026c, 0x0);
	write_mreg32(0x5a200270, 0x5A200000);
	write_mreg32(0x5a200274, 0x0);

	/* region 5 */
	write_mreg32(0x5a200278, 0x60201000);
	write_mreg32(0x5a20027c, 0x0);
	write_mreg32(0x5a200280, 0x60202FFF);
	write_mreg32(0x5a200284, 0x0);
	write_mreg32(0x5a200288, 0x5A216000);
	write_mreg32(0x5a20028c, 0x0);

	/* region 6 */
	write_mreg32(0x5a200290, 0x60203000);
	write_mreg32(0x5a200294, 0x0);
	write_mreg32(0x5a200298, 0x60203FFF);
	write_mreg32(0x5a20029c, 0x0);
	write_mreg32(0x5a2002a0, 0xA7400000);
	write_mreg32(0x5a2002a4, 0x0);
#else
	/* region 0 */
	write_mreg32(0x5a200200, 0x60000000);
	write_mreg32(0x5a200204, 0x0);
	write_mreg32(0x5a200208, 0x6007FFFF);
	write_mreg32(0x5a20020c, 0x0);
	write_mreg32(0x5a200210, 0x60000000);
	write_mreg32(0x5a200214, 0x0);

	/* region 1 */
	write_mreg32(0x5a200218, 0x60080000);
	write_mreg32(0x5a20021c, 0x0);
	write_mreg32(0x5a200220, 0x60080FFF);
	write_mreg32(0x5a200224, 0x0);
	write_mreg32(0x5a200228, 0x5A200000);
	write_mreg32(0x5a20022c, 0x0);

	/* region 2 */
	write_mreg32(0x5a200230, 0x60081000);
	write_mreg32(0x5a200234, 0x0);
	write_mreg32(0x5a200238, 0x60082FFF);
	write_mreg32(0x5a20023c, 0x0);
	write_mreg32(0x5a200240, 0x5A216000);
	write_mreg32(0x5a200244, 0x0);

	/* region 3: efuse */
	write_mreg32(0x5a200248, 0x60083000);
	write_mreg32(0x5a20024c, 0x0);
	write_mreg32(0x5a200250, 0x60083FFF);
	write_mreg32(0x5a200254, 0x0);
	write_mreg32(0x5a200258, 0xA7400000);
	write_mreg32(0x5a20025c, 0x0);
#endif

	write_mreg32(0x5a200104, 0x1);

	/* BAR 0 inbound atu, to ensure efuse accessible in bootrom */
	write_mreg32(0x58400840, APSS_S5_AP_SRAM_BASE);

	/* max payload size */
#if (defined(IPU_X6000) || defined(IPU_X6000_VS) || defined(IPU_X6000_LM) ||   \
     defined(IPU_X6000_DS))
	val = read_mreg32(HSIO_S4_PCIE_CONTROLLER_X8_REGISTER_BASE + 0xc8);
	write_mreg32(HSIO_S4_PCIE_CONTROLLER_X8_REGISTER_BASE + 0xc8,
		     (val & 0xFFFFFF1F) | (0x3 << 5));
#endif

	/* sync efuse block243 bit31 to 0x6007FABC */
	write_mreg32(EXT_INIT_BACKUP_ADDR, 0x80000000);

#if defined(IPU_X6000_LM)
	write_mreg32(X6000_PROD_MAGIC_ADDR, X6000_LM_MAGIC);
#elif defined(IPU_X6000_DS)
	write_mreg32(X6000_PROD_MAGIC_ADDR, X6000_DS_MAGIC);
#elif defined(IPU_X6000_VS)
	write_mreg32(X6000_PROD_MAGIC_ADDR, 0x0);
#elif defined(IPU_X6000)
	write_mreg32(X6000_PROD_MAGIC_ADDR, 0xdeadbeef);
#endif

	/* disable relaxed ordering*/
	val = read_mreg32(HSIO_S4_PCIE_CONTROLLER_X8_REGISTER_BASE + 0xc8);
	val &= ~(0x1 << 4);
	write_mreg32(HSIO_S4_PCIE_CONTROLLER_X8_REGISTER_BASE + 0xc8, val);

	/* pull up */
	//write_mreg32(IOMUX_AP_PWM1_ADDR, (val &
	//		(~(IOMUX_PINREG_WPD_U_IOPAD_AP_PWM1_WPD_MASK |
	//		IOMUX_PINREG_WPU_U_IOPAD_AP_PWM1_WPU_MASK))) |
	//		(0x1 << IOMUX_PINREG_WPU_U_IOPAD_AP_PWM1_WPU_POS));

	/* print */
	eprintf(version);
	memcpy((void *)APSRAM_PCIEFW_VER_BASE_ADDR, version, strlen(version) - 2);
	write_mreg8((void *)(APSRAM_PCIEFW_VER_BASE_ADDR + strlen(version) - 2), '\0');
	val = read_mreg32(LASTWORD_RESET_SOURCE_ADDR);
	eprintf(resetsrc, val);
	write_mreg32(0x6007FA7C, read_mreg32(0x6007FA78));
	write_mreg32(0x6007FA78, read_mreg32(0x6007FA74));
	write_mreg32(0x6007FA74, read_mreg32(0x6007FA70));
	write_mreg32(0x6007FA70, val);

	/* set bootstate and all dies' ipcmnet valid flag */
#if (defined(IPU_X6000) || defined(IPU_X6000_VS) || defined(IPU_X6000_LM) ||   \
     defined(IPU_X6000_DS))
	int die_id;
	write_mreg32(0x6007FD00, 0x0);
	write_mreg32(0x6007FA40, 0x0);
	for (die_id = 1; die_id <= 8; die_id++) {
		write_mreg32((0x6007FA40 + die_id * 4), 0xDE10DE10);
		write_mreg32((0x6007FD00 + die_id * 4), 0x0);
	}
	spi_nor_read_flash(NOR_FLASH_ID_BASE_ADDR, (u8 *)APSRAM_ID_BASE_ADDR,
			   READ_SIZE_NOR_TO_APSRAM);
#elif defined(IPU_X2000)
	spi_nor_read_flash(NOR_FLASH_ID_BASE_ADDR, (u8 *)APSRAM_ID_BASE_ADDR,
			   READ_SIZE_NOR_TO_APSRAM);
#endif
	return 0;
}


//#define IRQ_TEST
#ifdef IRQ_TEST
#define SCS_BASE            (0xE000E000UL)                             /*!< System Control Space Base Address */
#define SCB_BASE            (SCS_BASE +  0x0D00UL)                     /*!< System Control Block Base Address */
#define SET_VTOR(addr)      (*(volatile u32*)(SCB_BASE + 0x008) = (u32)(addr))

typedef void (*isr_func_t)(void);

typedef struct {
    u32* initial_sp;
    isr_func_t reset_handler;
    isr_func_t nmi_handler;
    isr_func_t hardfault_handler;
    isr_func_t memmanage_handler;
    isr_func_t busfault_handler;
    isr_func_t usagefault_handler;
    u32 reserved[4];
    isr_func_t svcall_handler;
    isr_func_t debugmon_handler;
    u32 reserved2;
    isr_func_t pendsv_handler;
    isr_func_t systick_handler;
    isr_func_t irq_handlers[128];
} isr_vector_table_t;

extern unsigned int __StackTop;
extern void NMI_Handler(void);
extern void Reset_Handler(void);
extern void HardFault_Handler(void);
extern void MemManage_Handler(void);
extern void BusFault_Handler(void);
extern void UsageFault_Handler(void);
extern void SVC_Handler(void);
extern void DebugMon_Handler(void);
extern void PendSV_Handler(void);
extern void SysTick_Handler(void);

extern void wdt_close(void);
extern void wdt_clear_irq_flag(void);
extern void watchdog_start(unsigned int period);

char ext_init_log[] PCIE_DATA = "ext_init.......\r\n";
char default_handler_log[] PCIE_DATA = "default_handler_ext Interrupt Handler....\r\n";
char wdt_init_log[] PCIE_DATA = "wdt init...\r\n";


PCIE_TEXT void default_handler_ext(void)
{
    NVIC_ClearPendingIRQ(AONPERI_WDT_STAR__WDT_IRQ);
	NVIC_DisableIRQ(AONPERI_WDT_STAR__WDT_IRQ);
    eprintf(default_handler_log);
    wdt_clear_irq_flag();
	wdt_close();
    watchdog_start(0xa);
    eprintf(wdt_init_log);
}

__attribute__((aligned(512))) PCIE_DATA isr_vector_table_t vector_table_ext = {
    .initial_sp = (u32*)0x2003fff0,
    .reset_handler = Reset_Handler,
    .nmi_handler = NMI_Handler,
    .hardfault_handler = HardFault_Handler,
    .memmanage_handler = MemManage_Handler,
    .busfault_handler = BusFault_Handler,
    .usagefault_handler = UsageFault_Handler,
    .svcall_handler = SVC_Handler,
    .debugmon_handler = DebugMon_Handler,
    .pendsv_handler = PendSV_Handler,
    .systick_handler = SysTick_Handler,
    //.irq_handlers[8] = default_handler_ext,
    .irq_handlers = { [0 ... 127] = default_handler_ext }
};

PCIE_TEXT_FIRST void ext_init(void)
{
    eprintf(ext_init_log);
    SET_VTOR(&vector_table_ext);
    watchdog_start(0xa);
    pcie_ep_init_ext();
}
#endif

```

# pcie_main_ex.c 程序总结

  这是一个针对 IPU 系列芯片（X2000/X5000/X6000 及其变种）的 PCIe 固件初始化程序。

  主要功能

  pcie_ep_init_ext() 函数是核心，执行 PCIe Endpoint 的完整初始化流程：

  1. 电源和复位控制 (行 120-202)

  - 配置 HSIO 系统电源控制
  - 区分冷启动和热复位 (hot_reset_flag)
  - 配置 Torrent PHY 寄存器（仅 ASIC）
  - 逐步释放 PCIe 相关模块的复位信号

  2. PHY 时钟稳定等待 (行 219-244)

  - 根据 group_id 采用不同策略等待 PHY 时钟稳定
  - group_id=1: 无限循环等待
  - group_id=0: 最多等待 200 次，超时返回错误

  3. PCIe 链路配置 (行 246-298)

  - 设置 PCIe 代数和带宽：
    - X2000: Gen2 x2
    - X5000/X6000: Gen3 x8
  - 配置最大 NP Outstanding 限制
  - 禁用顺序检查（X6000/DS）

  4. BAR 配置 (行 304-358)

  不同芯片型号有不同的 BAR 布局：
```
  ┌───────┬───────────────────────────────┐
  │ 芯片  │           BAR 配置            │
  ├───────┼───────────────────────────────┤
  │ X2000 │ BAR0 16M + BAR2 16M           │
  ├───────┼───────────────────────────────┤
  │ X5000 │ BAR0 16M + BAR2 64M           │
  ├───────┼───────────────────────────────┤
  │ X6000 │ 根据 group_id 配置 2-4 个 BAR │
  └───────┴───────────────────────────────┘
```
  5. APT 地址映射 (行 367-458)

  配置 6-7 个地址转换区域，映射 SRAM、寄存器、efuse 等

  6. 设备信息存储 (行 473-523)

  - 写入产品魔数（X6000 系列）
  - 打印固件版本
  - 将版本号存入 APSRAM
  - 记录复位源
  - 从 SPI NOR Flash 读取设备 ID/序列号到 APSRAM

  可选功能：中断测试 (#ifdef IRQ_TEST)

  - 定义中断向量表
  - ext_init(): 设置 VTOR，启动看门狗，调用初始化

  关键地址定义
```
  ┌────────────┬─────────────────────┐
  │    地址    │        用途         │
  ├────────────┼─────────────────────┤
  │ 0x6007FA20 │ PCIe 固件版本存储   │
  ├────────────┼─────────────────────┤
  │ 0x6007FA84 │ 设备 ID/序列号存储  │
  ├────────────┼─────────────────────┤
  │ 0x6007FAB8 │ X6000 产品魔数      │
  ├────────────┼─────────────────────┤
  │ 0x5A200908 │ PCIe 模式控制寄存器 │
  └────────────┴─────────────────────┘
```

