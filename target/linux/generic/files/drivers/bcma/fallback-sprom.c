// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BCMA Fallback SPROM Driver
 *
 * Copyright (C) 2020 Álvaro Fernández Rojas <noltari@gmail.com>
 * Copyright (C) 2014 Jonas Gorski <jonas.gorski@gmail.com>
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008 Florian Fainelli <f.fainelli@gmail.com>
 */

#include <linux/bcma/bcma.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include "fallback-sprom.h"

#define BCMA_FBS_MAX_SIZE 468

/* SPROM Extraction */
#define SPOFF(offset)	((offset) / sizeof(u16))

#define SPEX(_outvar, _offset, _mask, _shift)	\
	out->_outvar = ((in[SPOFF(_offset)] & (_mask)) >> (_shift))

#define SPEX32(_outvar, _offset, _mask, _shift)	\
	out->_outvar = ((((u32)in[SPOFF((_offset)+2)] << 16 | \
			   in[SPOFF(_offset)]) & (_mask)) >> (_shift))

#define SPEX_ARRAY8(_field, _offset, _mask, _shift)	\
	do {	\
		SPEX(_field[0], _offset +  0, _mask, _shift);	\
		SPEX(_field[1], _offset +  2, _mask, _shift);	\
		SPEX(_field[2], _offset +  4, _mask, _shift);	\
		SPEX(_field[3], _offset +  6, _mask, _shift);	\
		SPEX(_field[4], _offset +  8, _mask, _shift);	\
		SPEX(_field[5], _offset + 10, _mask, _shift);	\
		SPEX(_field[6], _offset + 12, _mask, _shift);	\
		SPEX(_field[7], _offset + 14, _mask, _shift);	\
	} while (0)

struct bcma_fbs {
	struct device *dev;
	struct list_head list;
	struct ssb_sprom sprom;
	u32 pci_bus;
	u32 pci_dev;
	bool devid_override;
};

static DEFINE_SPINLOCK(bcma_fbs_lock);
static struct list_head bcma_fbs_list = LIST_HEAD_INIT(bcma_fbs_list);

int bcma_get_fallback_sprom(struct bcma_bus *bus, struct ssb_sprom *out)
{
	struct bcma_fbs *pos;
	u32 pci_bus, pci_dev;

	if (bus->hosttype != BCMA_HOSTTYPE_PCI)
		return -ENOENT;

	pci_bus = bus->host_pci->bus->number;
	pci_dev = PCI_SLOT(bus->host_pci->devfn);

	list_for_each_entry(pos, &bcma_fbs_list, list) {
		if (pos->pci_bus != pci_bus ||
		    pos->pci_dev != pci_dev)
		    	continue;

		if (pos->devid_override)
			bus->host_pci->device = pos->sprom.dev_id;

		memcpy(out, &pos->sprom, sizeof(struct ssb_sprom));
		dev_info(pos->dev, "requested by [%x:%x]",
			 pos->pci_bus, pos->pci_dev);

		return 0;
	}

	pr_err("unable to fill SPROM for [%x:%x]\n", pci_bus, pci_dev);

	return -EINVAL;
}

static s8 sprom_extract_antgain(const u16 *in, u16 offset, u16 mask, u16 shift)
{
	u16 v;
	u8 gain;

	v = in[SPOFF(offset)];
	gain = (v & mask) >> shift;
	if (gain == 0xFF) {
		gain = 8; /* If unset use 2dBm */
	} else {
		/* Q5.2 Fractional part is stored in 0xC0 */
		gain = ((gain & 0xC0) >> 6) | ((gain & 0x3F) << 2);
	}

	return (s8)gain;
}

static void sprom_extract_r8(struct ssb_sprom *out, const u16 *in)
{
	static const u16 pwr_info_offset[] = {
		SSB_SROM8_PWR_INFO_CORE0, SSB_SROM8_PWR_INFO_CORE1,
		SSB_SROM8_PWR_INFO_CORE2, SSB_SROM8_PWR_INFO_CORE3
	};
	u16 o;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(pwr_info_offset) !=
		     ARRAY_SIZE(out->core_pwr_info));

	SPEX(board_rev, SSB_SPROM8_BOARDREV, ~0, 0);
	SPEX(board_type, SSB_SPROM1_SPID, ~0, 0);

	SPEX(txpid2g[0], SSB_SPROM4_TXPID2G01, SSB_SPROM4_TXPID2G0,
	     SSB_SPROM4_TXPID2G0_SHIFT);
	SPEX(txpid2g[1], SSB_SPROM4_TXPID2G01, SSB_SPROM4_TXPID2G1,
	     SSB_SPROM4_TXPID2G1_SHIFT);
	SPEX(txpid2g[2], SSB_SPROM4_TXPID2G23, SSB_SPROM4_TXPID2G2,
	     SSB_SPROM4_TXPID2G2_SHIFT);
	SPEX(txpid2g[3], SSB_SPROM4_TXPID2G23, SSB_SPROM4_TXPID2G3,
	     SSB_SPROM4_TXPID2G3_SHIFT);

	SPEX(txpid5gl[0], SSB_SPROM4_TXPID5GL01, SSB_SPROM4_TXPID5GL0,
	     SSB_SPROM4_TXPID5GL0_SHIFT);
	SPEX(txpid5gl[1], SSB_SPROM4_TXPID5GL01, SSB_SPROM4_TXPID5GL1,
	     SSB_SPROM4_TXPID5GL1_SHIFT);
	SPEX(txpid5gl[2], SSB_SPROM4_TXPID5GL23, SSB_SPROM4_TXPID5GL2,
	     SSB_SPROM4_TXPID5GL2_SHIFT);
	SPEX(txpid5gl[3], SSB_SPROM4_TXPID5GL23, SSB_SPROM4_TXPID5GL3,
	     SSB_SPROM4_TXPID5GL3_SHIFT);

	SPEX(txpid5g[0], SSB_SPROM4_TXPID5G01, SSB_SPROM4_TXPID5G0,
	     SSB_SPROM4_TXPID5G0_SHIFT);
	SPEX(txpid5g[1], SSB_SPROM4_TXPID5G01, SSB_SPROM4_TXPID5G1,
	     SSB_SPROM4_TXPID5G1_SHIFT);
	SPEX(txpid5g[2], SSB_SPROM4_TXPID5G23, SSB_SPROM4_TXPID5G2,
	     SSB_SPROM4_TXPID5G2_SHIFT);
	SPEX(txpid5g[3], SSB_SPROM4_TXPID5G23, SSB_SPROM4_TXPID5G3,
	     SSB_SPROM4_TXPID5G3_SHIFT);

	SPEX(txpid5gh[0], SSB_SPROM4_TXPID5GH01, SSB_SPROM4_TXPID5GH0,
	     SSB_SPROM4_TXPID5GH0_SHIFT);
	SPEX(txpid5gh[1], SSB_SPROM4_TXPID5GH01, SSB_SPROM4_TXPID5GH1,
	     SSB_SPROM4_TXPID5GH1_SHIFT);
	SPEX(txpid5gh[2], SSB_SPROM4_TXPID5GH23, SSB_SPROM4_TXPID5GH2,
	     SSB_SPROM4_TXPID5GH2_SHIFT);
	SPEX(txpid5gh[3], SSB_SPROM4_TXPID5GH23, SSB_SPROM4_TXPID5GH3,
	     SSB_SPROM4_TXPID5GH3_SHIFT);

	SPEX(boardflags_lo, SSB_SPROM8_BFLLO, ~0, 0);
	SPEX(boardflags_hi, SSB_SPROM8_BFLHI, ~0, 0);
	SPEX(boardflags2_lo, SSB_SPROM8_BFL2LO, ~0, 0);
	SPEX(boardflags2_hi, SSB_SPROM8_BFL2HI, ~0, 0);

	SPEX(alpha2[0], SSB_SPROM8_CCODE, 0xff00, 8);
	SPEX(alpha2[1], SSB_SPROM8_CCODE, 0x00ff, 0);

	/* Extract core's power info */
	for (i = 0; i < ARRAY_SIZE(pwr_info_offset); i++) {
		o = pwr_info_offset[i];
		SPEX(core_pwr_info[i].itssi_2g, o + SSB_SROM8_2G_MAXP_ITSSI,
			SSB_SPROM8_2G_ITSSI, SSB_SPROM8_2G_ITSSI_SHIFT);
		SPEX(core_pwr_info[i].maxpwr_2g, o + SSB_SROM8_2G_MAXP_ITSSI,
			SSB_SPROM8_2G_MAXP, 0);

		SPEX(core_pwr_info[i].pa_2g[0], o + SSB_SROM8_2G_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_2g[1], o + SSB_SROM8_2G_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_2g[2], o + SSB_SROM8_2G_PA_2, ~0, 0);

		SPEX(core_pwr_info[i].itssi_5g, o + SSB_SROM8_5G_MAXP_ITSSI,
			SSB_SPROM8_5G_ITSSI, SSB_SPROM8_5G_ITSSI_SHIFT);
		SPEX(core_pwr_info[i].maxpwr_5g, o + SSB_SROM8_5G_MAXP_ITSSI,
			SSB_SPROM8_5G_MAXP, 0);
		SPEX(core_pwr_info[i].maxpwr_5gh, o + SSB_SPROM8_5GHL_MAXP,
			SSB_SPROM8_5GH_MAXP, 0);
		SPEX(core_pwr_info[i].maxpwr_5gl, o + SSB_SPROM8_5GHL_MAXP,
			SSB_SPROM8_5GL_MAXP, SSB_SPROM8_5GL_MAXP_SHIFT);

		SPEX(core_pwr_info[i].pa_5gl[0], o + SSB_SROM8_5GL_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gl[1], o + SSB_SROM8_5GL_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gl[2], o + SSB_SROM8_5GL_PA_2, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[0], o + SSB_SROM8_5G_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[1], o + SSB_SROM8_5G_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[2], o + SSB_SROM8_5G_PA_2, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[0], o + SSB_SROM8_5GH_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[1], o + SSB_SROM8_5GH_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[2], o + SSB_SROM8_5GH_PA_2, ~0, 0);
	}

	SPEX(fem.ghz2.tssipos, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_TSSIPOS,
	     SSB_SROM8_FEM_TSSIPOS_SHIFT);
	SPEX(fem.ghz2.extpa_gain, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_EXTPA_GAIN,
	     SSB_SROM8_FEM_EXTPA_GAIN_SHIFT);
	SPEX(fem.ghz2.pdet_range, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_PDET_RANGE,
	     SSB_SROM8_FEM_PDET_RANGE_SHIFT);
	SPEX(fem.ghz2.tr_iso, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_TR_ISO,
	     SSB_SROM8_FEM_TR_ISO_SHIFT);
	SPEX(fem.ghz2.antswlut, SSB_SPROM8_FEM2G, SSB_SROM8_FEM_ANTSWLUT,
	     SSB_SROM8_FEM_ANTSWLUT_SHIFT);

	SPEX(fem.ghz5.tssipos, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_TSSIPOS,
	     SSB_SROM8_FEM_TSSIPOS_SHIFT);
	SPEX(fem.ghz5.extpa_gain, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_EXTPA_GAIN,
	     SSB_SROM8_FEM_EXTPA_GAIN_SHIFT);
	SPEX(fem.ghz5.pdet_range, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_PDET_RANGE,
	     SSB_SROM8_FEM_PDET_RANGE_SHIFT);
	SPEX(fem.ghz5.tr_iso, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_TR_ISO,
	     SSB_SROM8_FEM_TR_ISO_SHIFT);
	SPEX(fem.ghz5.antswlut, SSB_SPROM8_FEM5G, SSB_SROM8_FEM_ANTSWLUT,
	     SSB_SROM8_FEM_ANTSWLUT_SHIFT);

	SPEX(ant_available_a, SSB_SPROM8_ANTAVAIL, SSB_SPROM8_ANTAVAIL_A,
	     SSB_SPROM8_ANTAVAIL_A_SHIFT);
	SPEX(ant_available_bg, SSB_SPROM8_ANTAVAIL, SSB_SPROM8_ANTAVAIL_BG,
	     SSB_SPROM8_ANTAVAIL_BG_SHIFT);
	SPEX(maxpwr_bg, SSB_SPROM8_MAXP_BG, SSB_SPROM8_MAXP_BG_MASK, 0);
	SPEX(itssi_bg, SSB_SPROM8_MAXP_BG, SSB_SPROM8_ITSSI_BG,
	     SSB_SPROM8_ITSSI_BG_SHIFT);
	SPEX(maxpwr_a, SSB_SPROM8_MAXP_A, SSB_SPROM8_MAXP_A_MASK, 0);
	SPEX(itssi_a, SSB_SPROM8_MAXP_A, SSB_SPROM8_ITSSI_A,
	     SSB_SPROM8_ITSSI_A_SHIFT);
	SPEX(maxpwr_ah, SSB_SPROM8_MAXP_AHL, SSB_SPROM8_MAXP_AH_MASK, 0);
	SPEX(maxpwr_al, SSB_SPROM8_MAXP_AHL, SSB_SPROM8_MAXP_AL_MASK,
	     SSB_SPROM8_MAXP_AL_SHIFT);
	SPEX(gpio0, SSB_SPROM8_GPIOA, SSB_SPROM8_GPIOA_P0, 0);
	SPEX(gpio1, SSB_SPROM8_GPIOA, SSB_SPROM8_GPIOA_P1,
	     SSB_SPROM8_GPIOA_P1_SHIFT);
	SPEX(gpio2, SSB_SPROM8_GPIOB, SSB_SPROM8_GPIOB_P2, 0);
	SPEX(gpio3, SSB_SPROM8_GPIOB, SSB_SPROM8_GPIOB_P3,
	     SSB_SPROM8_GPIOB_P3_SHIFT);
	SPEX(tri2g, SSB_SPROM8_TRI25G, SSB_SPROM8_TRI2G, 0);
	SPEX(tri5g, SSB_SPROM8_TRI25G, SSB_SPROM8_TRI5G,
	     SSB_SPROM8_TRI5G_SHIFT);
	SPEX(tri5gl, SSB_SPROM8_TRI5GHL, SSB_SPROM8_TRI5GL, 0);
	SPEX(tri5gh, SSB_SPROM8_TRI5GHL, SSB_SPROM8_TRI5GH,
	     SSB_SPROM8_TRI5GH_SHIFT);
	SPEX(rxpo2g, SSB_SPROM8_RXPO, SSB_SPROM8_RXPO2G,
	     SSB_SPROM8_RXPO2G_SHIFT);
	SPEX(rxpo5g, SSB_SPROM8_RXPO, SSB_SPROM8_RXPO5G,
	     SSB_SPROM8_RXPO5G_SHIFT);
	SPEX(rssismf2g, SSB_SPROM8_RSSIPARM2G, SSB_SPROM8_RSSISMF2G, 0);
	SPEX(rssismc2g, SSB_SPROM8_RSSIPARM2G, SSB_SPROM8_RSSISMC2G,
	     SSB_SPROM8_RSSISMC2G_SHIFT);
	SPEX(rssisav2g, SSB_SPROM8_RSSIPARM2G, SSB_SPROM8_RSSISAV2G,
	     SSB_SPROM8_RSSISAV2G_SHIFT);
	SPEX(bxa2g, SSB_SPROM8_RSSIPARM2G, SSB_SPROM8_BXA2G,
	     SSB_SPROM8_BXA2G_SHIFT);
	SPEX(rssismf5g, SSB_SPROM8_RSSIPARM5G, SSB_SPROM8_RSSISMF5G, 0);
	SPEX(rssismc5g, SSB_SPROM8_RSSIPARM5G, SSB_SPROM8_RSSISMC5G,
	     SSB_SPROM8_RSSISMC5G_SHIFT);
	SPEX(rssisav5g, SSB_SPROM8_RSSIPARM5G, SSB_SPROM8_RSSISAV5G,
	     SSB_SPROM8_RSSISAV5G_SHIFT);
	SPEX(bxa5g, SSB_SPROM8_RSSIPARM5G, SSB_SPROM8_BXA5G,
	     SSB_SPROM8_BXA5G_SHIFT);

	SPEX(pa0b0, SSB_SPROM8_PA0B0, ~0, 0);
	SPEX(pa0b1, SSB_SPROM8_PA0B1, ~0, 0);
	SPEX(pa0b2, SSB_SPROM8_PA0B2, ~0, 0);
	SPEX(pa1b0, SSB_SPROM8_PA1B0, ~0, 0);
	SPEX(pa1b1, SSB_SPROM8_PA1B1, ~0, 0);
	SPEX(pa1b2, SSB_SPROM8_PA1B2, ~0, 0);
	SPEX(pa1lob0, SSB_SPROM8_PA1LOB0, ~0, 0);
	SPEX(pa1lob1, SSB_SPROM8_PA1LOB1, ~0, 0);
	SPEX(pa1lob2, SSB_SPROM8_PA1LOB2, ~0, 0);
	SPEX(pa1hib0, SSB_SPROM8_PA1HIB0, ~0, 0);
	SPEX(pa1hib1, SSB_SPROM8_PA1HIB1, ~0, 0);
	SPEX(pa1hib2, SSB_SPROM8_PA1HIB2, ~0, 0);
	SPEX(cck2gpo, SSB_SPROM8_CCK2GPO, ~0, 0);
	SPEX32(ofdm2gpo, SSB_SPROM8_OFDM2GPO, ~0, 0);
	SPEX32(ofdm5glpo, SSB_SPROM8_OFDM5GLPO, ~0, 0);
	SPEX32(ofdm5gpo, SSB_SPROM8_OFDM5GPO, ~0, 0);
	SPEX32(ofdm5ghpo, SSB_SPROM8_OFDM5GHPO, ~0, 0);

	/* Extract the antenna gain values. */
	out->antenna_gain.a0 = sprom_extract_antgain(in,
						     SSB_SPROM8_AGAIN01,
						     SSB_SPROM8_AGAIN0,
						     SSB_SPROM8_AGAIN0_SHIFT);
	out->antenna_gain.a1 = sprom_extract_antgain(in,
						     SSB_SPROM8_AGAIN01,
						     SSB_SPROM8_AGAIN1,
						     SSB_SPROM8_AGAIN1_SHIFT);
	out->antenna_gain.a2 = sprom_extract_antgain(in,
						     SSB_SPROM8_AGAIN23,
						     SSB_SPROM8_AGAIN2,
						     SSB_SPROM8_AGAIN2_SHIFT);
	out->antenna_gain.a3 = sprom_extract_antgain(in,
						     SSB_SPROM8_AGAIN23,
						     SSB_SPROM8_AGAIN3,
						     SSB_SPROM8_AGAIN3_SHIFT);

	SPEX(leddc_on_time, SSB_SPROM8_LEDDC, SSB_SPROM8_LEDDC_ON,
	     SSB_SPROM8_LEDDC_ON_SHIFT);
	SPEX(leddc_off_time, SSB_SPROM8_LEDDC, SSB_SPROM8_LEDDC_OFF,
	     SSB_SPROM8_LEDDC_OFF_SHIFT);

	SPEX(txchain, SSB_SPROM8_TXRXC, SSB_SPROM8_TXRXC_TXCHAIN,
	     SSB_SPROM8_TXRXC_TXCHAIN_SHIFT);
	SPEX(rxchain, SSB_SPROM8_TXRXC, SSB_SPROM8_TXRXC_RXCHAIN,
	     SSB_SPROM8_TXRXC_RXCHAIN_SHIFT);
	SPEX(antswitch, SSB_SPROM8_TXRXC, SSB_SPROM8_TXRXC_SWITCH,
	     SSB_SPROM8_TXRXC_SWITCH_SHIFT);

	SPEX(opo, SSB_SPROM8_OFDM2GPO, 0x00ff, 0);

	SPEX_ARRAY8(mcs2gpo, SSB_SPROM8_2G_MCSPO, ~0, 0);
	SPEX_ARRAY8(mcs5gpo, SSB_SPROM8_5G_MCSPO, ~0, 0);
	SPEX_ARRAY8(mcs5glpo, SSB_SPROM8_5GL_MCSPO, ~0, 0);
	SPEX_ARRAY8(mcs5ghpo, SSB_SPROM8_5GH_MCSPO, ~0, 0);

	SPEX(rawtempsense, SSB_SPROM8_RAWTS, SSB_SPROM8_RAWTS_RAWTEMP,
	     SSB_SPROM8_RAWTS_RAWTEMP_SHIFT);
	SPEX(measpower, SSB_SPROM8_RAWTS, SSB_SPROM8_RAWTS_MEASPOWER,
	     SSB_SPROM8_RAWTS_MEASPOWER_SHIFT);
	SPEX(tempsense_slope, SSB_SPROM8_OPT_CORRX,
	     SSB_SPROM8_OPT_CORRX_TEMP_SLOPE,
	     SSB_SPROM8_OPT_CORRX_TEMP_SLOPE_SHIFT);
	SPEX(tempcorrx, SSB_SPROM8_OPT_CORRX, SSB_SPROM8_OPT_CORRX_TEMPCORRX,
	     SSB_SPROM8_OPT_CORRX_TEMPCORRX_SHIFT);
	SPEX(tempsense_option, SSB_SPROM8_OPT_CORRX,
	     SSB_SPROM8_OPT_CORRX_TEMP_OPTION,
	     SSB_SPROM8_OPT_CORRX_TEMP_OPTION_SHIFT);
	SPEX(freqoffset_corr, SSB_SPROM8_HWIQ_IQSWP,
	     SSB_SPROM8_HWIQ_IQSWP_FREQ_CORR,
	     SSB_SPROM8_HWIQ_IQSWP_FREQ_CORR_SHIFT);
	SPEX(iqcal_swp_dis, SSB_SPROM8_HWIQ_IQSWP,
	     SSB_SPROM8_HWIQ_IQSWP_IQCAL_SWP,
	     SSB_SPROM8_HWIQ_IQSWP_IQCAL_SWP_SHIFT);
	SPEX(hw_iqcal_en, SSB_SPROM8_HWIQ_IQSWP, SSB_SPROM8_HWIQ_IQSWP_HW_IQCAL,
	     SSB_SPROM8_HWIQ_IQSWP_HW_IQCAL_SHIFT);

	SPEX(bw40po, SSB_SPROM8_BW40PO, ~0, 0);
	SPEX(cddpo, SSB_SPROM8_CDDPO, ~0, 0);
	SPEX(stbcpo, SSB_SPROM8_STBCPO, ~0, 0);
	SPEX(bwduppo, SSB_SPROM8_BWDUPPO, ~0, 0);

	SPEX(tempthresh, SSB_SPROM8_THERMAL, SSB_SPROM8_THERMAL_TRESH,
	     SSB_SPROM8_THERMAL_TRESH_SHIFT);
	SPEX(tempoffset, SSB_SPROM8_THERMAL, SSB_SPROM8_THERMAL_OFFSET,
	     SSB_SPROM8_THERMAL_OFFSET_SHIFT);
	SPEX(phycal_tempdelta, SSB_SPROM8_TEMPDELTA,
	     SSB_SPROM8_TEMPDELTA_PHYCAL,
	     SSB_SPROM8_TEMPDELTA_PHYCAL_SHIFT);
	SPEX(temps_period, SSB_SPROM8_TEMPDELTA, SSB_SPROM8_TEMPDELTA_PERIOD,
	     SSB_SPROM8_TEMPDELTA_PERIOD_SHIFT);
	SPEX(temps_hysteresis, SSB_SPROM8_TEMPDELTA,
	     SSB_SPROM8_TEMPDELTA_HYSTERESIS,
	     SSB_SPROM8_TEMPDELTA_HYSTERESIS_SHIFT);
}

static int sprom_extract(struct bcma_fbs *priv, const u16 *in, u16 size)
{
	struct ssb_sprom *out = &priv->sprom;

	memset(out, 0, sizeof(*out));

	out->revision = in[size - 1] & 0x00FF;
	if (out->revision < 8 || out->revision > 11) {
		dev_warn(priv->dev,
			 "Unsupported SPROM revision %d detected."
			 " Will extract v8\n",
			 out->revision);
		out->revision = 8;
	}

	sprom_extract_r8(out, in);

	return 0;
}

static void bcma_fbs_fixup(struct bcma_fbs *priv, u16 *sprom)
{
	struct device_node *node = priv->dev->of_node;
	u32 fixups, off, val;
	int i = 0;

	if (!of_get_property(node, "brcm,sprom-fixups", &fixups))
		return;

	fixups /= sizeof(u32);

	dev_info(priv->dev, "patching SPROM with %u fixups...\n", fixups >> 1);

	while (i < fixups) {
		if (of_property_read_u32_index(node, "brcm,sprom-fixups",
					       i++, &off)) {
			dev_err(priv->dev, "error reading fixup[%u] offset\n",
				i - 1);
			return;
		}

		if (of_property_read_u32_index(node, "brcm,sprom-fixups",
					       i++, &val)) {
			dev_err(priv->dev, "error reading fixup[%u] value\n",
				i - 1);
			return;
		}

		dev_dbg(priv->dev, "fixup[%d]=0x%04x\n", off, val);

		sprom[off] = val;
	}
}

static bool sprom_override_devid(struct bcma_fbs *priv, struct ssb_sprom *out,
				 const u16 *in)
{
	SPEX(dev_id, 0x0060, 0xFFFF, 0);
	return !!out->dev_id;
}

static void bcma_fbs_set(struct bcma_fbs *priv, struct device_node *node)
{
	struct ssb_sprom *sprom = &priv->sprom;
	const struct firmware *fw;
	const char *sprom_name;
	int err;

	if (of_property_read_string(node, "brcm,sprom", &sprom_name))
		sprom_name = NULL;

	if (sprom_name) {
		err = request_firmware_direct(&fw, sprom_name, priv->dev);
		if (err)
			dev_err(priv->dev, "%s load error\n", sprom_name);
	} else {
		err = -ENOENT;
	}

	if (err) {
		sprom->revision = 0x02;
		sprom->board_rev = 0x0017;
		sprom->country_code = 0x00;
		sprom->ant_available_bg = 0x03;
		sprom->pa0b0 = 0x15ae;
		sprom->pa0b1 = 0xfa85;
		sprom->pa0b2 = 0xfe8d;
		sprom->pa1b0 = 0xffff;
		sprom->pa1b1 = 0xffff;
		sprom->pa1b2 = 0xffff;
		sprom->gpio0 = 0xff;
		sprom->gpio1 = 0xff;
		sprom->gpio2 = 0xff;
		sprom->gpio3 = 0xff;
		sprom->maxpwr_bg = 0x4c;
		sprom->itssi_bg = 0x00;
		sprom->boardflags_lo = 0x2848;
		sprom->boardflags_hi = 0x0000;
		priv->devid_override = false;

		dev_warn(priv->dev, "using basic SPROM\n");
	} else {
		size_t size = min(fw->size, (size_t) BCMA_FBS_MAX_SIZE);
		u16 tmp_sprom[BCMA_FBS_MAX_SIZE >> 1];
		u32 i, j;

		for (i = 0, j = 0; i < size; i += 2, j++)
			tmp_sprom[j] = (fw->data[i] << 8) | fw->data[i + 1];

		release_firmware(fw);
		bcma_fbs_fixup(priv, tmp_sprom);
		sprom_extract(priv, tmp_sprom, size >> 1);

		priv->devid_override = sprom_override_devid(priv, sprom,
							    tmp_sprom);
	}
}

static int bcma_fbs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct bcma_fbs *priv;
	unsigned long flags;
	u8 mac[ETH_ALEN];

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	bcma_fbs_set(priv, node);

	of_property_read_u32(node, "pci-bus", &priv->pci_bus);
	of_property_read_u32(node, "pci-dev", &priv->pci_dev);

	of_get_mac_address(node, mac);
	if (is_valid_ether_addr(mac)) {
		dev_info(dev, "mtd mac %pM\n", mac);
	} else {
		eth_random_addr(mac);
		dev_info(dev, "random mac %pM\n", mac);
	}

	memcpy(priv->sprom.il0mac, mac, ETH_ALEN);
 	memcpy(priv->sprom.et0mac, mac, ETH_ALEN);
 	memcpy(priv->sprom.et1mac, mac, ETH_ALEN);
	memcpy(priv->sprom.et2mac, mac, ETH_ALEN);

	spin_lock_irqsave(&bcma_fbs_lock, flags);
	list_add(&priv->list, &bcma_fbs_list);
	spin_unlock_irqrestore(&bcma_fbs_lock, flags);

	dev_info(dev, "registered SPROM for [%x:%x]\n",
		 priv->pci_bus, priv->pci_dev);

	return 0;
}

static const struct of_device_id bcma_fbs_of_match[] = {
	{ .compatible = "brcm,bcma-sprom", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcma_fbs_of_match);

static struct platform_driver bcma_fbs_driver = {
	.probe = bcma_fbs_probe,
	.driver	= {
		.name = "bcma-sprom",
		.of_match_table = bcma_fbs_of_match,
	},
};

int __init bcma_fbs_register(void)
{
	return platform_driver_register(&bcma_fbs_driver);
}
