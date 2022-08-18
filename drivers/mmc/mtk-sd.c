// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek SD/MMC Card Interface driver
 *
 * Copyright (C) 2018 MediaTek Inc.
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <clk.h>
#include <common.h>
#include <dm.h>
#include <dm/pinctrl.h>
#include <errno.h>
#include <stdbool.h>
#include <malloc.h>
#include <mmc.h>
#include <asm/gpio.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#define MSDC_CFG			0x0
#define   MSDC_CFG_HS400_CK_MODE_EXT	BIT(22)
#define   MSDC_CFG_CKMOD_EXT_M		0x03
#define   MSDC_CFG_CKMOD_EXT_S		20
#define   MSDC_CFG_CKDIV_EXT_M		0xfff
#define   MSDC_CFG_CKDIV_EXT_S		8
#define   MSDC_CFG_HS400_CK_MODE	BIT(18)
#define   MSDC_CFG_CKMOD_M		0x03
#define   MSDC_CFG_CKMOD_S		16
#define   MSDC_CFG_CKDIV_M		0xff
#define   MSDC_CFG_CKDIV_S		8
#define   MSDC_CFG_CKSTB		BIT(7)
#define   MSDC_CFG_PIO			BIT(3)
#define   MSDC_CFG_RST			BIT(2)
#define   MSDC_CFG_CKPDN		BIT(1)
#define   MSDC_CFG_MODE			BIT(0)

#define MSDC_IOCON			0x04
#define   MSDC_IOCON_W_DSPL		BIT(8)
#define   MSDC_IOCON_DSPL		BIT(2)
#define   MSDC_IOCON_RSPL		BIT(1)

#define MSDC_PS				0x08
#define   MSDC_PS_SDWP			BIT(31)
#define   MSDC_PS_DAT0			BIT(16)
#define   MSDC_PS_CDDBCE_M		0x0f
#define   MSDC_PS_CDDBCE_S		12
#define   MSDC_PS_CDSTS			BIT(1)
#define   MSDC_PS_CDEN			BIT(0)

#define MSDC_INT			0x0c
#define MSDC_INTEN			0x10
#define   MSDC_INT_ACMDRDY		BIT(3)
#define   MSDC_INT_ACMDTMO		BIT(4)
#define   MSDC_INT_ACMDCRCERR		BIT(5)
#define   MSDC_INT_CMDRDY		BIT(8)
#define   MSDC_INT_CMDTMO		BIT(9)
#define   MSDC_INT_RSPCRCERR		BIT(10)
#define   MSDC_INT_XFER_COMPL		BIT(12)
#define   MSDC_INT_DATTMO		BIT(14)
#define   MSDC_INT_DATCRCERR		BIT(15)

#define MSDC_FIFOCS			0x14
#define   MSDC_FIFOCS_CLR		BIT(31)
#define   MSDC_FIFOCS_TXCNT_M		0xff
#define   MSDC_FIFOCS_TXCNT_S		16
#define   MSDC_FIFOCS_RXCNT_M		0xff
#define   MSDC_FIFOCS_RXCNT_S		0

#define MSDC_TXDATA			0x18
#define MSDC_RXDATA			0x1c

#define SDC_CFG				0x30
#define   SDC_CFG_DTOC_M		0xff
#define   SDC_CFG_DTOC_S		24
#define   SDC_CFG_SDIOIDE		BIT(20)
#define   SDC_CFG_SDIO			BIT(19)
#define   SDC_CFG_BUSWIDTH_M		0x03
#define   SDC_CFG_BUSWIDTH_S		16

#define SDC_CMD				0x34
#define   SDC_CMD_BLK_LEN_M		0xfff
#define   SDC_CMD_BLK_LEN_S		16
#define   SDC_CMD_STOP			BIT(14)
#define   SDC_CMD_WR			BIT(13)
#define   SDC_CMD_DTYPE_M		0x03
#define   SDC_CMD_DTYPE_S		11
#define   SDC_CMD_RSPTYP_M		0x07
#define   SDC_CMD_RSPTYP_S		7
#define   SDC_CMD_CMD_M			0x3f
#define   SDC_CMD_CMD_S			0

#define SDC_ARG				0x38

#define SDC_STS				0x3c
#define   SDC_STS_CMDBUSY		BIT(1)
#define   SDC_STS_SDCBUSY		BIT(0)

#define SDC_RESP0			0x40
#define SDC_RESP1			0x44
#define SDC_RESP2			0x48
#define SDC_RESP3			0x4c

#define SDC_BLK_NUM			0x50

#define SDC_ADV_CFG0			0x64
#define   SDC_RX_ENHANCE_EN		BIT(20)

#define MSDC_PATCH_BIT			0xb0
#define   MSDC_INT_DAT_LATCH_CK_SEL_M	0x07
#define   MSDC_INT_DAT_LATCH_CK_SEL_S	7

#define MSDC_PATCH_BIT1			0xb4
#define   MSDC_PB1_STOP_DLY_M		0x0f
#define   MSDC_PB1_STOP_DLY_S		8

#define MSDC_PATCH_BIT2			0xb8
#define   MSDC_PB2_CRCSTSENSEL_M	0x07
#define   MSDC_PB2_CRCSTSENSEL_S	29
#define   MSDC_PB2_CFGCRCSTS		BIT(28)
#define   MSDC_PB2_RESPSTSENSEL_M	0x07
#define   MSDC_PB2_RESPSTSENSEL_S	16
#define   MSDC_PB2_CFGRESP		BIT(15)
#define   MSDC_PB2_RESPWAIT_M		0x03
#define   MSDC_PB2_RESPWAIT_S		2

#define MSDC_PAD_TUNE			0xec
#define   MSDC_PAD_TUNE_CMDRRDLY_M	0x1f
#define   MSDC_PAD_TUNE_CMDRRDLY_S	22
#define   MSDC_PAD_TUNE_CMD_SEL		BIT(21)
#define   MSDC_PAD_TUNE_CMDRDLY_M	0x1f
#define   MSDC_PAD_TUNE_CMDRDLY_S	16
#define   MSDC_PAD_TUNE_RXDLYSEL	BIT(15)
#define   MSDC_PAD_TUNE_RD_SEL		BIT(13)
#define   MSDC_PAD_TUNE_DATRRDLY_M	0x1f
#define   MSDC_PAD_TUNE_DATRRDLY_S	8
#define   MSDC_PAD_TUNE_DATWRDLY_M	0x1f
#define   MSDC_PAD_TUNE_DATWRDLY_S	0

#define MSDC_PAD_TUNE0			0xf0

#define PAD_DS_TUNE			0x188

#define EMMC50_CFG0			0x208
#define   EMMC50_CFG_CFCSTS_SEL		BIT(4)

#define SDC_FIFO_CFG			0x228
#define   SDC_FIFO_CFG_WRVALIDSEL	BIT(24)
#define   SDC_FIFO_CFG_RDVALIDSEL	BIT(25)

/* SDC_CFG_BUSWIDTH */
#define MSDC_BUS_1BITS			0x0
#define MSDC_BUS_4BITS			0x1
#define MSDC_BUS_8BITS			0x2

#define MSDC_FIFO_SIZE			128

#define PAD_DELAY_MAX			32

#define DEFAULT_CD_DEBOUNCE		8

#define CMD_INTS_MASK	\
	(MSDC_INT_CMDRDY | MSDC_INT_RSPCRCERR | MSDC_INT_CMDTMO)

#define DATA_INTS_MASK	\
	(MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)

struct msdc_compatible {
	u8 clk_div_bits;
	bool pad_tune0;
	bool async_fifo;
	bool data_tune;
	bool busy_check;
	bool stop_clk_fix;
	bool enhance_rx;
};

struct msdc_delay_phase {
	u8 maxlen;
	u8 start;
	u8 final_phase;
};

struct msdc_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

struct msdc_tune_para {
	u32 iocon;
	u32 pad_tune;
};

struct msdc_host {
	void __iomem *base;
	struct mmc *mmc;

	struct msdc_compatible *dev_comp;

	struct clk src_clk;

	u32 mclk;
	u32 src_clk_freq;
	u32 sclk;

	u32 timeout_ns;
	u32 timeout_clks;

	u32 hs200_cmd_int_delay;
	u32 hs200_write_int_delay;
	u32 latch_ck;
	u32 r_smpl;
	bool hs400_mode;

	uint last_resp_type;
	uint last_data_write;

	enum bus_mode timing;

	struct msdc_tune_para def_tune_para;
	struct msdc_tune_para saved_tune_para;
};

static void msdc_reset_hw(struct msdc_host *host)
{
	u32 reg;

	setbits_le32(host->base + MSDC_CFG, MSDC_CFG_RST);

	readl_poll_timeout(host->base + MSDC_CFG, reg,
			   !(reg & MSDC_CFG_RST), 1000000);
}

static void msdc_fifo_clr(struct msdc_host *host)
{
	u32 reg;

	setbits_le32(host->base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);

	readl_poll_timeout(host->base + MSDC_FIFOCS, reg,
			   !(reg & MSDC_FIFOCS_CLR), 1000000);
}

static u32 msdc_fifo_rx_bytes(struct msdc_host *host)
{
	return (readl(host->base + MSDC_FIFOCS) >> MSDC_FIFOCS_RXCNT_S) &
		MSDC_FIFOCS_RXCNT_M;
}

static u32 msdc_fifo_tx_bytes(struct msdc_host *host)
{
	return (readl(host->base + MSDC_FIFOCS) >> MSDC_FIFOCS_TXCNT_S) &
		MSDC_FIFOCS_TXCNT_M;
}

static u32 msdc_cmd_find_resp(struct msdc_host *host, struct mmc_cmd *cmd)
{
	u32 resp;

	switch (cmd->resp_type) {
		/* Actually, R1, R5, R6, R7 are the same */
	case MMC_RSP_R1:
		resp = 0x1;
		break;
	case MMC_RSP_R1b:
		resp = 0x7;
		break;
	case MMC_RSP_R2:
		resp = 0x2;
		break;
	case MMC_RSP_R3:
		resp = 0x3;
		break;
	case MMC_RSP_NONE:
	default:
		resp = 0x0;
		break;
	}

	return resp;
}

static u32 msdc_cmd_prepare_raw_cmd(struct msdc_host *host,
				    struct mmc_cmd *cmd,
				    struct mmc_data *data)
{
	u32 opcode = cmd->cmdidx;
	u32 resp_type = msdc_cmd_find_resp(host, cmd);
	uint blocksize = 0;
	u32 dtype = 0;
	u32 rawcmd = 0;

	switch (opcode) {
	case MMC_CMD_WRITE_MULTIPLE_BLOCK:
	case MMC_CMD_READ_MULTIPLE_BLOCK:
		dtype = 2;
		break;
	case MMC_CMD_WRITE_SINGLE_BLOCK:
	case MMC_CMD_READ_SINGLE_BLOCK:
	case SD_CMD_APP_SEND_SCR:
		dtype = 1;
		break;
	case SD_CMD_SWITCH_FUNC: /* same as MMC_CMD_SWITCH */
	case SD_CMD_SEND_IF_COND: /* same as MMC_CMD_SEND_EXT_CSD */
	case SD_CMD_APP_SD_STATUS: /* same as MMC_CMD_SEND_STATUS */
		if (data)
			dtype = 1;
	}

	if (data) {
		if (data->flags == MMC_DATA_WRITE)
			rawcmd |= SDC_CMD_WR;

		if (data->blocks > 1)
			dtype = 2;

		blocksize = data->blocksize;
	}

	rawcmd |= ((opcode & SDC_CMD_CMD_M) << SDC_CMD_CMD_S) |
		((resp_type & SDC_CMD_RSPTYP_M) << SDC_CMD_RSPTYP_S) |
		((blocksize & SDC_CMD_BLK_LEN_M) << SDC_CMD_BLK_LEN_S) |
		((dtype & SDC_CMD_DTYPE_M) << SDC_CMD_DTYPE_S);

	if (opcode == MMC_CMD_STOP_TRANSMISSION)
		rawcmd |= SDC_CMD_STOP;

	return rawcmd;
}

static int msdc_cmd_done(struct msdc_host *host, int events,
			 struct mmc_cmd *cmd)
{
	u32 *rsp = cmd->response;
	int ret = 0;

	if (cmd->resp_type & MMC_RSP_PRESENT) {
		if (cmd->resp_type & MMC_RSP_136) {
			rsp[0] = readl(host->base + SDC_RESP3);
			rsp[1] = readl(host->base + SDC_RESP2);
			rsp[2] = readl(host->base + SDC_RESP1);
			rsp[3] = readl(host->base + SDC_RESP0);
		} else {
			rsp[0] = readl(host->base + SDC_RESP0);
		}
	}

	if (!(events & MSDC_INT_CMDRDY)) {
		if (cmd->cmdidx != MMC_CMD_SEND_TUNING_BLOCK &&
		    cmd->cmdidx != MMC_CMD_SEND_TUNING_BLOCK_HS200)
			/*
			 * should not clear fifo/interrupt as the tune data
			 * may have alreay come.
			 */
			msdc_reset_hw(host);

		if (events & MSDC_INT_CMDTMO)
			ret = -ETIMEDOUT;
		else
			ret = -EIO;
	}

	return ret;
}

static bool msdc_cmd_is_ready(struct msdc_host *host)
{
	int ret;
	u32 reg;

	/* The max busy time we can endure is 20ms */
	ret = readl_poll_timeout(host->base + SDC_STS, reg,
				 !(reg & SDC_STS_CMDBUSY), 20000);

	if (ret) {
		pr_err("CMD bus busy detected\n");
		msdc_reset_hw(host);
		return false;
	}

	if (host->last_resp_type == MMC_RSP_R1b && host->last_data_write) {
		ret = readl_poll_timeout(host->base + MSDC_PS, reg,
					 reg & MSDC_PS_DAT0, 1000000);

		if (ret) {
			pr_err("Card stuck in programming state!\n");
			msdc_reset_hw(host);
			return false;
		}
	}

	return true;
}

static int msdc_start_command(struct msdc_host *host, struct mmc_cmd *cmd,
			      struct mmc_data *data)
{
	u32 rawcmd;
	u32 status;
	u32 blocks = 0;
	int ret;

	if (!msdc_cmd_is_ready(host))
		return -EIO;

	msdc_fifo_clr(host);

	host->last_resp_type = cmd->resp_type;
	host->last_data_write = 0;

	rawcmd = msdc_cmd_prepare_raw_cmd(host, cmd, data);

	if (data)
		blocks = data->blocks;

	writel(CMD_INTS_MASK, host->base + MSDC_INT);
	writel(blocks, host->base + SDC_BLK_NUM);
	writel(cmd->cmdarg, host->base + SDC_ARG);
	writel(rawcmd, host->base + SDC_CMD);

	ret = readl_poll_timeout(host->base + MSDC_INT, status,
				 status & CMD_INTS_MASK, 1000000);

	if (ret)
		status = MSDC_INT_CMDTMO;

	return msdc_cmd_done(host, status, cmd);
}

static void msdc_fifo_read(struct msdc_host *host, u8 *buf, u32 size)
{
	u32 *wbuf;

	while ((size_t)buf % 4) {
		*buf++ = readb(host->base + MSDC_RXDATA);
		size--;
	}

	wbuf = (u32 *)buf;
	while (size >= 4) {
		*wbuf++ = readl(host->base + MSDC_RXDATA);
		size -= 4;
	}

	buf = (u8 *)wbuf;
	while (size) {
		*buf++ = readb(host->base + MSDC_RXDATA);
		size--;
	}
}

static void msdc_fifo_write(struct msdc_host *host, const u8 *buf, u32 size)
{
	const u32 *wbuf;

	while ((size_t)buf % 4) {
		writeb(*buf++, host->base + MSDC_TXDATA);
		size--;
	}

	wbuf = (const u32 *)buf;
	while (size >= 4) {
		writel(*wbuf++, host->base + MSDC_TXDATA);
		size -= 4;
	}

	buf = (const u8 *)wbuf;
	while (size) {
		writeb(*buf++, host->base + MSDC_TXDATA);
		size--;
	}
}

static int msdc_pio_read(struct msdc_host *host, u8 *ptr, u32 size)
{
	u32 status;
	u32 chksz;
	int ret = 0;

	while (1) {
		status = readl(host->base + MSDC_INT);
		writel(status, host->base + MSDC_INT);
		status &= DATA_INTS_MASK;

		if (status & MSDC_INT_DATCRCERR) {
			ret = -EIO;
			break;
		}

		if (status & MSDC_INT_DATTMO) {
			ret = -ETIMEDOUT;
			break;
		}

		if (status & MSDC_INT_XFER_COMPL) {
			if (size) {
				pr_err("data not fully read\n");
				ret = -EIO;
			}

			break;
		}

		chksz = min(size, (u32)MSDC_FIFO_SIZE);

		if (msdc_fifo_rx_bytes(host) >= chksz) {
			msdc_fifo_read(host, ptr, chksz);
			ptr += chksz;
			size -= chksz;
		}
	}

	return ret;
}

static int msdc_pio_write(struct msdc_host *host, const u8 *ptr, u32 size)
{
	u32 status;
	u32 chksz;
	int ret = 0;

	while (1) {
		status = readl(host->base + MSDC_INT);
		writel(status, host->base + MSDC_INT);
		status &= DATA_INTS_MASK;

		if (status & MSDC_INT_DATCRCERR) {
			ret = -EIO;
			break;
		}

		if (status & MSDC_INT_DATTMO) {
			ret = -ETIMEDOUT;
			break;
		}

		if (status & MSDC_INT_XFER_COMPL) {
			if (size) {
				pr_err("data not fully written\n");
				ret = -EIO;
			}

			break;
		}

		chksz = min(size, (u32)MSDC_FIFO_SIZE);

		if (MSDC_FIFO_SIZE - msdc_fifo_tx_bytes(host) >= chksz) {
			msdc_fifo_write(host, ptr, chksz);
			ptr += chksz;
			size -= chksz;
		}
	}

	return ret;
}

static int msdc_start_data(struct msdc_host *host, struct mmc_data *data)
{
	u32 size;
	int ret;

	if (data->flags == MMC_DATA_WRITE)
		host->last_data_write = 1;

	writel(DATA_INTS_MASK, host->base + MSDC_INT);

	size = data->blocks * data->blocksize;

	if (data->flags == MMC_DATA_WRITE)
		ret = msdc_pio_write(host, (const u8 *)data->src, size);
	else
		ret = msdc_pio_read(host, (u8 *)data->dest, size);

	if (ret) {
		msdc_reset_hw(host);
		msdc_fifo_clr(host);
	}

	return ret;
}

static int msdc_ops_send_cmd(struct udevice *dev, struct mmc_cmd *cmd,
			     struct mmc_data *data)
{
	struct msdc_host *host = dev_get_priv(dev);
	int ret;

	ret = msdc_start_command(host, cmd, data);
	if (ret)
		return ret;

	if (data)
		return msdc_start_data(host, data);

	return 0;
}

static void msdc_set_timeout(struct msdc_host *host, u32 ns, u32 clks)
{
	u32 timeout, clk_ns;
	u32 mode = 0;

	host->timeout_ns = ns;
	host->timeout_clks = clks;

	if (host->sclk == 0) {
		timeout = 0;
	} else {
		clk_ns = 1000000000UL / host->sclk;
		timeout = (ns + clk_ns - 1) / clk_ns + clks;
		/* unit is 1048576 sclk cycles */
		timeout = (timeout + (0x1 << 20) - 1) >> 20;
		if (host->dev_comp->clk_div_bits == 8)
			mode = (readl(host->base + MSDC_CFG) >>
				MSDC_CFG_CKMOD_S) & MSDC_CFG_CKMOD_M;
		else
			mode = (readl(host->base + MSDC_CFG) >>
				MSDC_CFG_CKMOD_EXT_S) & MSDC_CFG_CKMOD_EXT_M;
		/* DDR mode will double the clk cycles for data timeout */
		timeout = mode >= 2 ? timeout * 2 : timeout;
		timeout = timeout > 1 ? timeout - 1 : 0;
		timeout = timeout > 255 ? 255 : timeout;
	}

	clrsetbits_le32(host->base + SDC_CFG, SDC_CFG_DTOC_M << SDC_CFG_DTOC_S,
			timeout << SDC_CFG_DTOC_S);
}

static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
	u32 val = readl(host->base + SDC_CFG);

	val &= ~(SDC_CFG_BUSWIDTH_M << SDC_CFG_BUSWIDTH_S);

	switch (width) {
	default:
	case 1:
		val |= (MSDC_BUS_1BITS << SDC_CFG_BUSWIDTH_S);
		break;
	case 4:
		val |= (MSDC_BUS_4BITS << SDC_CFG_BUSWIDTH_S);
		break;
	case 8:
		val |= (MSDC_BUS_8BITS << SDC_CFG_BUSWIDTH_S);
		break;
	}

	writel(val, host->base + SDC_CFG);
}

static void msdc_set_mclk(struct msdc_host *host, enum bus_mode timing, u32 hz)
{
	u32 mode;
	u32 div;
	u32 sclk;
	u32 reg;

	if (!hz) {
		host->mclk = 0;
		clrbits_le32(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
		return;
	}

	if (host->dev_comp->clk_div_bits == 8)
		clrbits_le32(host->base + MSDC_CFG, MSDC_CFG_HS400_CK_MODE);
	else
		clrbits_le32(host->base + MSDC_CFG,
			     MSDC_CFG_HS400_CK_MODE_EXT);

	if (timing == UHS_DDR50 || timing == MMC_DDR_52) {
		mode = 0x2; /* ddr mode and use divisor */

		if (hz >= (host->src_clk_freq >> 2)) {
			div = 0; /* mean div = 1/4 */
			sclk = host->src_clk_freq >> 2; /* sclk = clk / 4 */
		} else {
			div = (host->src_clk_freq + ((hz << 2) - 1)) /
			       (hz << 2);
			sclk = (host->src_clk_freq >> 2) / div;
			div = (div >> 1);
		}
	} else if (hz >= host->src_clk_freq) {
		mode = 0x1; /* no divisor */
		div = 0;
		sclk = host->src_clk_freq;
	} else {
		mode = 0x0; /* use divisor */
		if (hz >= (host->src_clk_freq >> 1)) {
			div = 0; /* mean div = 1/2 */
			sclk = host->src_clk_freq >> 1; /* sclk = clk / 2 */
		} else {
			div = (host->src_clk_freq + ((hz << 2) - 1)) /
			       (hz << 2);
			sclk = (host->src_clk_freq >> 2) / div;
		}
	}

	clrbits_le32(host->base + MSDC_CFG, MSDC_CFG_CKPDN);

	if (host->dev_comp->clk_div_bits == 8) {
		div = min(div, (u32)MSDC_CFG_CKDIV_M);
		clrsetbits_le32(host->base + MSDC_CFG,
				(MSDC_CFG_CKMOD_M << MSDC_CFG_CKMOD_S) |
				(MSDC_CFG_CKDIV_M << MSDC_CFG_CKDIV_S),
				(mode << MSDC_CFG_CKMOD_S) |
				(div << MSDC_CFG_CKDIV_S));
	} else {
		div = min(div, (u32)MSDC_CFG_CKDIV_EXT_M);
		clrsetbits_le32(host->base + MSDC_CFG,
				(MSDC_CFG_CKMOD_EXT_M << MSDC_CFG_CKMOD_EXT_S) |
				(MSDC_CFG_CKDIV_EXT_M << MSDC_CFG_CKDIV_EXT_S),
				(mode << MSDC_CFG_CKMOD_S) |
				(div << MSDC_CFG_CKDIV_EXT_S));
	}

	readl_poll_timeout(host->base + MSDC_CFG, reg,
			   reg & MSDC_CFG_CKSTB, 1000000);

	setbits_le32(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
	host->sclk = sclk;
	host->mclk = hz;
	host->timing = timing;

	/* needed because clk changed. */
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks);

	/*
	 * mmc_select_hs400() will drop to 50Mhz and High speed mode,
	 * tune result of hs200/200Mhz is not suitable for 50Mhz
	 */
	if (host->sclk <= 52000000) {
		writel(host->def_tune_para.iocon, host->base + MSDC_IOCON);
		writel(host->def_tune_para.pad_tune,
		       host->base + MSDC_PAD_TUNE);
	} else {
		writel(host->saved_tune_para.iocon, host->base + MSDC_IOCON);
		writel(host->saved_tune_para.pad_tune,
		       host->base + MSDC_PAD_TUNE);
	}

	dev_dbg(dev, "sclk: %d, timing: %d\n", host->sclk, timing);
}

static int msdc_ops_set_ios(struct udevice *dev)
{
	struct msdc_plat *plat = dev_get_platdata(dev);
	struct msdc_host *host = dev_get_priv(dev);
	struct mmc *mmc = &plat->mmc;
	uint clock = mmc->clock;

	msdc_set_buswidth(host, mmc->bus_width);

	if (mmc->clk_disable)
		clock = 0;
	else if (clock < mmc->cfg->f_min)
		clock = mmc->cfg->f_min;

	if (host->mclk != clock || host->timing != mmc->selected_mode)
		msdc_set_mclk(host, mmc->selected_mode, clock);

	return 0;
}

static int msdc_ops_get_cd(struct udevice *dev)
{
	struct msdc_host *host = dev_get_priv(dev);
	u32 val;

	val = readl(host->base + MSDC_PS);

	return !(val & MSDC_PS_CDSTS);
}

static int msdc_ops_get_wp(struct udevice *dev)
{
	return 0;
}

#ifdef MMC_SUPPORTS_TUNING
static u32 test_delay_bit(u32 delay, u32 bit)
{
	bit %= PAD_DELAY_MAX;
	return delay & (1 << bit);
}

static int get_delay_len(u32 delay, u32 start_bit)
{
	int i;

	for (i = 0; i < (PAD_DELAY_MAX - start_bit); i++) {
		if (test_delay_bit(delay, start_bit + i) == 0)
			return i;
	}

	return PAD_DELAY_MAX - start_bit;
}

static struct msdc_delay_phase get_best_delay(struct msdc_host *host, u32 delay)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xff;
	struct msdc_delay_phase delay_phase = { 0, };

	if (delay == 0) {
		dev_err(dev, "phase error: [map:%x]\n", delay);
		delay_phase.final_phase = final_phase;
		return delay_phase;
	}

	while (start < PAD_DELAY_MAX) {
		len = get_delay_len(delay, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}

		start += len ? len : 1;
		if (len >= 12 && start_final < 4)
			break;
	}

	/* The rule is to find the smallest delay cell */
	if (start_final == 0)
		final_phase = (start_final + len_final / 3) % PAD_DELAY_MAX;
	else
		final_phase = (start_final + len_final / 2) % PAD_DELAY_MAX;

	dev_info(dev, "phase: [map:%x] [maxlen:%d] [final:%d]\n",
		 delay, len_final, final_phase);

	delay_phase.maxlen = len_final;
	delay_phase.start = start_final;
	delay_phase.final_phase = final_phase;
	return delay_phase;
}

static int msdc_tune_response(struct udevice *dev, u32 opcode)
{
	struct msdc_plat *plat = dev_get_platdata(dev);
	struct msdc_host *host = dev_get_priv(dev);
	struct mmc *mmc = &plat->mmc;
	u32 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0, };
	struct msdc_delay_phase internal_delay_phase;
	u8 final_delay, final_maxlen;
	u32 internal_delay = 0;
	u32 tune_reg = MSDC_PAD_TUNE;
	int cmd_err;
	int i, j;

	if (host->dev_comp->pad_tune0)
		tune_reg = MSDC_PAD_TUNE0;

	if (mmc->selected_mode == MMC_HS_200 ||
	    mmc->selected_mode == UHS_SDR104)
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_CMDRRDLY_M <<
				MSDC_PAD_TUNE_CMDRRDLY_S,
				host->hs200_cmd_int_delay <<
				MSDC_PAD_TUNE_CMDRRDLY_S);

	clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);

	for (i = 0; i < PAD_DELAY_MAX; i++) {
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_CMDRDLY_M <<
				MSDC_PAD_TUNE_CMDRDLY_S,
				i << MSDC_PAD_TUNE_CMDRDLY_S);

		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				rise_delay |= (1 << i);
			} else {
				rise_delay &= ~(1 << i);
				break;
			}
		}
	}

	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0; i < PAD_DELAY_MAX; i++) {
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_CMDRDLY_M <<
				MSDC_PAD_TUNE_CMDRDLY_S,
				i << MSDC_PAD_TUNE_CMDRDLY_S);

		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				fall_delay |= (1 << i);
			} else {
				fall_delay &= ~(1 << i);
				break;
			}
		}
	}

	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_CMDRDLY_M <<
				MSDC_PAD_TUNE_CMDRDLY_S,
				final_rise_delay.final_phase <<
				MSDC_PAD_TUNE_CMDRDLY_S);
		final_delay = final_rise_delay.final_phase;
	} else {
		setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_CMDRDLY_M <<
				MSDC_PAD_TUNE_CMDRDLY_S,
				final_fall_delay.final_phase <<
				MSDC_PAD_TUNE_CMDRDLY_S);
		final_delay = final_fall_delay.final_phase;
	}

	if (host->dev_comp->async_fifo || host->hs200_cmd_int_delay)
		goto skip_internal;

	for (i = 0; i < PAD_DELAY_MAX; i++) {
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_CMDRRDLY_M <<
				MSDC_PAD_TUNE_CMDRRDLY_S,
				i << MSDC_PAD_TUNE_CMDRRDLY_S);

		mmc_send_tuning(mmc, opcode, &cmd_err);
		if (!cmd_err)
			internal_delay |= (1 << i);
	}

	dev_err(dev, "Final internal delay: 0x%x\n", internal_delay);

	internal_delay_phase = get_best_delay(host, internal_delay);
	clrsetbits_le32(host->base + tune_reg,
			MSDC_PAD_TUNE_CMDRRDLY_M <<
			MSDC_PAD_TUNE_CMDRRDLY_S,
			internal_delay_phase.final_phase <<
			MSDC_PAD_TUNE_CMDRRDLY_S);

skip_internal:
	dev_err(dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_tune_data(struct udevice *dev, u32 opcode)
{
	struct msdc_plat *plat = dev_get_platdata(dev);
	struct msdc_host *host = dev_get_priv(dev);
	struct mmc *mmc = &plat->mmc;
	u32 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0, };
	u8 final_delay, final_maxlen;
	u32 tune_reg = MSDC_PAD_TUNE;
	int cmd_err;
	int i, ret;

	if (host->dev_comp->pad_tune0)
		tune_reg = MSDC_PAD_TUNE0;

	clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
	clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);

	for (i = 0; i < PAD_DELAY_MAX; i++) {
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_DATRRDLY_M <<
				MSDC_PAD_TUNE_DATRRDLY_S,
				i << MSDC_PAD_TUNE_DATRRDLY_S);

		ret = mmc_send_tuning(mmc, opcode, &cmd_err);
		if (!ret) {
			rise_delay |= (1 << i);
		} else if (cmd_err) {
			/* in this case, retune response is needed */
			ret = msdc_tune_response(dev, opcode);
			if (ret)
				break;
		}
	}

	final_rise_delay = get_best_delay(host, rise_delay);
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
	setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);

	for (i = 0; i < PAD_DELAY_MAX; i++) {
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_DATRRDLY_M <<
				MSDC_PAD_TUNE_DATRRDLY_S,
				i << MSDC_PAD_TUNE_DATRRDLY_S);

		ret = mmc_send_tuning(mmc, opcode, &cmd_err);
		if (!ret) {
			fall_delay |= (1 << i);
		} else if (cmd_err) {
			/* in this case, retune response is needed */
			ret = msdc_tune_response(dev, opcode);
			if (ret)
				break;
		}
	}

	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
		clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_DATRRDLY_M <<
				MSDC_PAD_TUNE_DATRRDLY_S,
				final_rise_delay.final_phase <<
				MSDC_PAD_TUNE_DATRRDLY_S);
		final_delay = final_rise_delay.final_phase;
	} else {
		setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
		setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_DATRRDLY_M <<
				MSDC_PAD_TUNE_DATRRDLY_S,
				final_fall_delay.final_phase <<
				MSDC_PAD_TUNE_DATRRDLY_S);
		final_delay = final_fall_delay.final_phase;
	}

	if (mmc->selected_mode == MMC_HS_200 ||
	    mmc->selected_mode == UHS_SDR104)
		clrsetbits_le32(host->base + tune_reg,
				MSDC_PAD_TUNE_DATWRDLY_M <<
				MSDC_PAD_TUNE_DATWRDLY_S,
				host->hs200_write_int_delay <<
				MSDC_PAD_TUNE_DATWRDLY_S);

	dev_err(dev, "Final data pad delay: %x\n", final_delay);

	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_execute_tuning(struct udevice *dev, uint opcode)
{
	struct msdc_host *host = dev_get_priv(dev);
	int ret;

	ret = msdc_tune_response(dev, opcode);
	if (ret == -EIO) {
		dev_err(dev, "Tune response fail!\n");
		return ret;
	}

	ret = msdc_tune_data(dev, opcode);
	if (ret == -EIO)
		dev_err(dev, "Tune data fail!\n");

	host->saved_tune_para.iocon = readl(host->base + MSDC_IOCON);
	host->saved_tune_para.pad_tune = readl(host->base + MSDC_PAD_TUNE);

	return ret;
}
#endif

static void msdc_init_hw(struct msdc_host *host)
{
	u32 val;
	u32 tune_reg = MSDC_PAD_TUNE;

	if (host->dev_comp->pad_tune0)
		tune_reg = MSDC_PAD_TUNE0;

	/* Configure to MMC/SD mode, clock free running */
	setbits_le32(host->base + MSDC_CFG, MSDC_CFG_MODE);

	/* Use PIO mode */
	setbits_le32(host->base + MSDC_CFG, MSDC_CFG_PIO);

	/* Reset */
	msdc_reset_hw(host);

	/* Enable card detection */
	clrsetbits_le32(host->base + MSDC_PS,
		MSDC_PS_CDDBCE_M << MSDC_PS_CDDBCE_S,
		(DEFAULT_CD_DEBOUNCE << MSDC_PS_CDDBCE_S) |
		MSDC_PS_CDEN);

	/* Clear all interrupts */
	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);

	/* Enable data & cmd interrupts */
	writel(DATA_INTS_MASK | CMD_INTS_MASK, host->base + MSDC_INTEN);

	writel(0, host->base + tune_reg);
	writel(0, host->base + MSDC_IOCON);

	if (host->r_smpl)
		setbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	else
		clrbits_le32(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);

	writel(0x403c0046, host->base + MSDC_PATCH_BIT);
	writel(0xffff4089, host->base + MSDC_PATCH_BIT1);

	if (host->dev_comp->stop_clk_fix)
		clrsetbits_le32(host->base + MSDC_PATCH_BIT1,
				MSDC_PB1_STOP_DLY_M << MSDC_PB1_STOP_DLY_S,
				3 << MSDC_PB1_STOP_DLY_S);

	if (host->dev_comp->busy_check)
		clrbits_le32(host->base + MSDC_PATCH_BIT1, (1 << 7));

	setbits_le32(host->base + EMMC50_CFG0, EMMC50_CFG_CFCSTS_SEL);

	if (host->dev_comp->async_fifo) {
		clrsetbits_le32(host->base + MSDC_PATCH_BIT2,
				MSDC_PB2_RESPWAIT_M << MSDC_PB2_RESPWAIT_S,
				3 << MSDC_PB2_RESPWAIT_S);

		if (host->dev_comp->enhance_rx) {
			setbits_le32(host->base + SDC_ADV_CFG0,
				     SDC_RX_ENHANCE_EN);
		} else {
			clrsetbits_le32(host->base + MSDC_PATCH_BIT2,
					MSDC_PB2_RESPSTSENSEL_M <<
					MSDC_PB2_RESPSTSENSEL_S,
					2 << MSDC_PB2_RESPSTSENSEL_S);
			clrsetbits_le32(host->base + MSDC_PATCH_BIT2,
					MSDC_PB2_CRCSTSENSEL_M <<
					MSDC_PB2_CRCSTSENSEL_S,
					2 << MSDC_PB2_CRCSTSENSEL_S);
		}

		/* use async fifo to avoid tune internal delay */
		clrbits_le32(host->base + MSDC_PATCH_BIT2,
			     MSDC_PB2_CFGRESP);
		clrbits_le32(host->base + MSDC_PATCH_BIT2,
			     MSDC_PB2_CFGCRCSTS);
	}

	if (host->dev_comp->data_tune) {
		setbits_le32(host->base + tune_reg,
			     MSDC_PAD_TUNE_RD_SEL | MSDC_PAD_TUNE_CMD_SEL);
		clrsetbits_le32(host->base + MSDC_PATCH_BIT,
				MSDC_INT_DAT_LATCH_CK_SEL_S <<
				MSDC_INT_DAT_LATCH_CK_SEL_M,
				host->latch_ck <<
				MSDC_INT_DAT_LATCH_CK_SEL_S);
	} else {
		/* choose clock tune */
		setbits_le32(host->base + tune_reg, MSDC_PAD_TUNE_RXDLYSEL);
	}

	/* Configure to enable SDIO mode otherwise sdio cmd5 won't work */
	setbits_le32(host->base + SDC_CFG, SDC_CFG_SDIO);

	/* disable detecting SDIO device interrupt function */
	clrbits_le32(host->base + SDC_CFG, SDC_CFG_SDIOIDE);

	/* Configure to default data timeout */
	clrsetbits_le32(host->base + SDC_CFG,
			SDC_CFG_DTOC_M << SDC_CFG_DTOC_S,
			3 << SDC_CFG_DTOC_S);

	if (host->dev_comp->stop_clk_fix) {
		clrbits_le32(host->base + SDC_FIFO_CFG,
			     SDC_FIFO_CFG_WRVALIDSEL);
		clrbits_le32(host->base + SDC_FIFO_CFG,
			     SDC_FIFO_CFG_RDVALIDSEL);
	}

	host->def_tune_para.iocon = readl(host->base + MSDC_IOCON);
	host->def_tune_para.pad_tune = readl(host->base + MSDC_PAD_TUNE);
}

static int msdc_drv_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct msdc_plat *plat = dev_get_platdata(dev);
	struct msdc_host *host = dev_get_priv(dev);
	struct mmc_config *cfg = &plat->cfg;
	int ret;

	cfg->name = dev->name;

	host->base = (void *)dev_read_addr(dev);
	if (!host->base)
		return -EINVAL;

	ret = mmc_of_parse(dev, cfg);
	if (ret)
		return ret;

	ret = clk_get_by_name(dev, "source", &host->src_clk);
	if (ret < 0)
		return ret;

	host->hs200_cmd_int_delay =
			dev_read_u32_default(dev, "cmd_int_delay", 0);
	host->hs200_write_int_delay =
			dev_read_u32_default(dev, "write_int_delay", 0);
	host->latch_ck = dev_read_u32_default(dev, "latch-ck", 0);
	host->r_smpl = dev_read_u32_default(dev, "r_smpl", 0);

	host->dev_comp = (struct msdc_compatible *)dev_get_driver_data(dev);

	host->src_clk_freq = clk_get_rate(&host->src_clk);

	if (host->dev_comp->clk_div_bits == 8)
		cfg->f_min = host->src_clk_freq / (4 * 255);
	else
		cfg->f_min = host->src_clk_freq / (4 * 4095);
	cfg->f_max = host->src_clk_freq / 2;

	cfg->b_max = 1024;
	cfg->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;

	host->mmc = &plat->mmc;
	host->timeout_ns = 100000000;
	host->timeout_clks = 3 * 1048576;

#ifdef PINCTRL
	pinctrl_select_state(dev, "default");
#endif

	msdc_init_hw(host);

	upriv->mmc = &plat->mmc;

	return 0;
}

static int msdc_drv_bind(struct udevice *dev)
{
	struct msdc_plat *plat = dev_get_platdata(dev);

	return mmc_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct dm_mmc_ops msdc_ops = {
	.send_cmd = msdc_ops_send_cmd,
	.set_ios = msdc_ops_set_ios,
	.get_cd = msdc_ops_get_cd,
	.get_wp = msdc_ops_get_wp,
#ifdef MMC_SUPPORTS_TUNING
	.execute_tuning = msdc_execute_tuning,
#endif
};

static const struct msdc_compatible mt7621_compat = {
	.clk_div_bits = 8,
	.pad_tune0 = false,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false
};

static const struct udevice_id msdc_ids[] = {
	{ .compatible = "mediatek,mt7621-mmc", .data = (ulong)&mt7621_compat },
	{}
};

U_BOOT_DRIVER(mtk_sd_drv) = {
	.name = "mtk_sd",
	.id = UCLASS_MMC,
	.of_match = msdc_ids,
	.bind = msdc_drv_bind,
	.probe = msdc_drv_probe,
	.ops = &msdc_ops,
	.platdata_auto_alloc_size = sizeof(struct msdc_plat),
	.priv_auto_alloc_size = sizeof(struct msdc_host),
};