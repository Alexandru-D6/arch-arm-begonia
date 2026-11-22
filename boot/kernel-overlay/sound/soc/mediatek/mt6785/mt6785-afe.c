// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal MT6785 DL1/VUL12/ADDA path for begonia.
 * Register programming derived from Xiaomi's begonia-p-oss MT6785 driver
 * (Copyright (c) 2018 MediaTek Inc.). Uses Linux's common AFE DMA/IRQ code.
 * Deliberately limited to 48 kHz, S16_LE, two channels during bring-up.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "mt6785-afe.h"
#include "mt6785-reg.h"
#include "../common/mtk-afe-fe-dai.h"
#include "../common/mtk-afe-platform-driver.h"

enum { DL1, VUL12, MEMIF_COUNT, ADDA = MEMIF_COUNT };

static const struct snd_pcm_hardware mt6785_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min = 256,
	.period_bytes_max = 64 * 1024,
	.periods_min = 3,
	.periods_max = 256,
	.buffer_bytes_max = 256 * 1024,
};

static int mt6785_fs(struct snd_pcm_substream *substream, unsigned int rate)
{
	/* AFE rate encoding differs from ADDA's SRC encoding. */
	return rate == 48000 ? 10 : -EINVAL;
}

static const struct mtk_base_memif_data mt6785_memifs[] = {
	[DL1] = {
		.name = "DL1", .id = DL1,
		.reg_ofs_base = AFE_DL1_BASE, .reg_ofs_cur = AFE_DL1_CUR,
		.reg_ofs_end = AFE_DL1_END,
		.reg_ofs_base_msb = AFE_DL1_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL1_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL1_END_MSB,
		.fs_reg = AFE_DL1_CON0, .fs_shift = DL1_MODE_SFT,
		.fs_maskbit = DL1_MODE_MASK,
		.mono_reg = AFE_DL1_CON0, .mono_shift = DL1_MONO_SFT,
		.enable_reg = AFE_DAC_CON0, .enable_shift = DL1_ON_SFT,
		.hd_reg = AFE_DL1_CON0, .hd_shift = DL1_HD_MODE_SFT,
		.hd_align_reg = -1,
		.agent_disable_reg = -1, .msb_reg = -1,
	},
	[VUL12] = {
		.name = "VUL12", .id = VUL12,
		.reg_ofs_base = AFE_VUL12_BASE, .reg_ofs_cur = AFE_VUL12_CUR,
		.reg_ofs_end = AFE_VUL12_END,
		.reg_ofs_base_msb = AFE_VUL12_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL12_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL12_END_MSB,
		.fs_reg = AFE_VUL12_CON0, .fs_shift = VUL12_MODE_SFT,
		.fs_maskbit = VUL12_MODE_MASK,
		.mono_reg = AFE_VUL12_CON0, .mono_shift = VUL12_MONO_SFT,
		.enable_reg = AFE_DAC_CON0, .enable_shift = VUL12_ON_SFT,
		.hd_reg = AFE_VUL12_CON0, .hd_shift = VUL12_HD_MODE_SFT,
		.hd_align_reg = -1,
		.agent_disable_reg = -1, .msb_reg = -1,
	},
};

static const struct mtk_base_irq_data mt6785_irqs[] = {
	{
		.id = 0, .irq_cnt_reg = AFE_IRQ_MCU_CNT0,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ0_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ0_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0, .irq_en_shift = 0,
		.irq_clr_reg = AFE_IRQ_MCU_CLR, .irq_clr_shift = 0,
	}, {
		.id = 1, .irq_cnt_reg = AFE_IRQ_MCU_CNT1,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ1_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ1_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0, .irq_en_shift = 1,
		.irq_clr_reg = AFE_IRQ_MCU_CLR, .irq_clr_shift = 1,
	},
};

static irqreturn_t mt6785_interrupt(int irq, void *data)
{
	struct mtk_base_afe *afe = data;
	unsigned int status;
	int i;

	if (regmap_read(afe->regmap, AFE_IRQ_MCU_STATUS, &status))
		return IRQ_NONE;
	status &= 3;
	if (!status)
		return IRQ_NONE;
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, status);
	for (i = 0; i < MEMIF_COUNT; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];

		if (memif->substream && memif->irq_usage >= 0 &&
		    (status & BIT(memif->irq_usage)))
			snd_pcm_period_elapsed(memif->substream);
	}
	return IRQ_HANDLED;
}

static int mt6785_adda_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct regmap *map = afe->regmap;

	if (params_rate(params) != 48000)
		return -EINVAL;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_write(map, AFE_ADDA_PREDIS_CON0, 0);
		regmap_write(map, AFE_ADDA_PREDIS_CON1, 0);
		/* 48 kHz SRC input (8), x8 output, unmute, gain enable. */
		regmap_update_bits(map, AFE_ADDA_DL_SRC2_CON0, GENMASK(31, 1),
				  (8U << 28) | (3 << 24) | (3 << 11) | BIT(1));
		regmap_write(map, AFE_ADDA_DL_SRC2_CON1, 0xf74f0000);
		regmap_update_bits(map, AFE_ADDA_DL_SDM_DCCOMP_CON,
				  ATTGAIN_CTL_MASK_SFT | USE_3RD_SDM_MASK_SFT,
				  0x1d << ATTGAIN_CTL_SFT);
		regmap_write(map, AFE_ADDA_DL_SDM_AUTO_RESET_CON,
			     0x400 | ADDA_SDM_AUTO_RESET_ONOFF_MASK_SFT);
	} else {
		regmap_update_bits(map, AFE_ADDA_TOP_CON0, BIT(0), 0);
		regmap_update_bits(map, AFE_ADDA_MTKAIF_RX_CFG0, BIT(0), 0);
		/* 48 kHz analog microphone, SRC mode 3. */
		regmap_update_bits(map, AFE_ADDA_UL_SRC_CON0, GENMASK(31, 1), 3 << 17);
	}
	return 0;
}

static const struct snd_soc_dai_ops mt6785_adda_ops = {
	.hw_params = mt6785_adda_params,
};

#define MT6785_STREAM(stream) { .stream_name = stream, .channels_min = 2, \
	.channels_max = 2, .rates = SNDRV_PCM_RATE_48000, \
	.formats = SNDRV_PCM_FMTBIT_S16_LE }

static struct snd_soc_dai_driver mt6785_dais[] = {
	{ .name = "DL1", .id = DL1, .playback = MT6785_STREAM("DL1"),
	  .ops = &mtk_afe_fe_ops },
	{ .name = "VUL12", .id = VUL12, .capture = MT6785_STREAM("VUL12"),
	  .ops = &mtk_afe_fe_ops },
	{ .name = "ADDA", .id = ADDA,
	  .playback = MT6785_STREAM("ADDA Playback"),
	  .capture = MT6785_STREAM("ADDA Capture"), .ops = &mt6785_adda_ops },
};

static int mt6785_ul_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *control, int event)
{
	/* Wait at least one 8 kHz sample before removing the AFE clock. */
	usleep_range(125, 150);
	return 0;
}

static const struct snd_soc_dapm_widget mt6785_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("MTKAIF RX FIFO", 0, AFE_AUD_PAD_TOP, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADDA Enable", 1, AFE_ADDA_UL_DL_CON0, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADDA DL Enable", 2, AFE_ADDA_DL_SRC2_CON0, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADDA UL Enable", 3, AFE_ADDA_UL_SRC_CON0, 0, 0,
			    mt6785_ul_event, SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("DAC Clock", AUDIO_TOP_CON0, 25, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Predis Clock", AUDIO_TOP_CON0,
			    26, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Clock", AUDIO_TOP_CON0, 24, 1, NULL, 0),
};

/* Fixed, independent routes: DL1 -> O3/O4; I3/I4 -> O21/O22 -> VUL12. */
static const struct snd_soc_dapm_route mt6785_routes[] = {
	{ "ADDA Playback", NULL, "DL1" },
	{ "VUL12", NULL, "ADDA Capture" },
	{ "ADDA Playback", NULL, "ADDA Enable" },
	{ "ADDA Playback", NULL, "ADDA DL Enable" },
	{ "ADDA Playback", NULL, "DAC Clock" },
	{ "ADDA Playback", NULL, "DAC Predis Clock" },
	{ "ADDA Capture", NULL, "ADDA Enable" },
	{ "ADDA Capture", NULL, "MTKAIF RX FIFO" },
	{ "ADDA Capture", NULL, "ADDA UL Enable" },
	{ "ADDA Capture", NULL, "ADC Clock" },
};

static int mt6785_component_probe(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, afe->regmap);
	return 0;
}

static const struct snd_soc_component_driver mt6785_component = {
	.name = "mt6785-afe-dai",
	.probe = mt6785_component_probe,
	.dapm_widgets = mt6785_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6785_widgets),
	.dapm_routes = mt6785_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6785_routes),
};

static int mt6785_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt6785_afe *priv = afe->platform_priv;
	int ret = clk_bulk_prepare_enable(ARRAY_SIZE(priv->clocks), priv->clocks);

	if (ret)
		return ret;
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, BIT(0), BIT(0));
	return 0;
}

static int mt6785_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt6785_afe *priv = afe->platform_priv;
	unsigned int value;
	int ret;

	regmap_update_bits(afe->regmap, AFE_DAC_CON0, BIT(0), 0);
	ret = regmap_read_poll_timeout(afe->regmap, AFE_DAC_MON, value,
				       !(value & AFE_ON_RETM_MASK_SFT), 20, 1000000);
	if (ret)
		return ret;
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, 3);
	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clocks), priv->clocks);
	return 0;
}

const struct dev_pm_ops mt6785_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mt6785_runtime_suspend, mt6785_runtime_resume, NULL)
};

static void mt6785_power_off(void *data)
{
	struct device *dev = data;

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
}

int mt6785_afe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_base_afe *afe;
	struct mt6785_afe *priv;
	static const char * const clock_names[] = {
		"infra", "audio", "bus", "afe", "mtkaif", "tml",
	};
	int i, ret, irq;

	afe = devm_kzalloc(dev, sizeof(*afe), GFP_KERNEL);
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!afe || !priv)
		return -ENOMEM;
	afe->dev = dev;
	afe->platform_priv = priv;
	platform_set_drvdata(pdev, afe);
	/* audsys owns the shared AFE/clock register window. */
	afe->regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "mediatek,audsys");
	if (IS_ERR(afe->regmap))
		return dev_err_probe(dev, PTR_ERR(afe->regmap), "audsys regmap\n");
	priv->topckgen = syscon_regmap_lookup_by_phandle(dev->of_node, "mediatek,topckgen");
	if (IS_ERR(priv->topckgen))
		return dev_err_probe(dev, PTR_ERR(priv->topckgen), "topckgen regmap\n");
	for (i = 0; i < ARRAY_SIZE(clock_names); i++)
		priv->clocks[i].id = clock_names[i];
	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(priv->clocks), priv->clocks);
	if (ret)
		return dev_err_probe(dev, ret, "audio clocks\n");
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	afe->memif_size = MEMIF_COUNT;
	afe->irqs_size = ARRAY_SIZE(mt6785_irqs);
	afe->memif = devm_kcalloc(dev, MEMIF_COUNT, sizeof(*afe->memif), GFP_KERNEL);
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs), GFP_KERNEL);
	if (!afe->memif || !afe->irqs)
		return -ENOMEM;
	mutex_init(&afe->irq_alloc_lock);
	for (i = 0; i < MEMIF_COUNT; i++) {
		afe->memif[i].data = &mt6785_memifs[i];
		afe->memif[i].irq_usage = -1;
		afe->irqs[i].irq_data = &mt6785_irqs[i];
	}
	afe->mtk_afe_hardware = &mt6785_pcm_hardware;
	afe->memif_fs = mt6785_fs;
	afe->irq_fs = mt6785_fs;
	/* Keep the domain on during bring-up: mixer and calibration registers
	 * share a non-cached syscon with audsys. Stream DAPM still gates ADDA.
	 */
	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		pm_runtime_disable(dev);
		return ret;
	}
	ret = devm_add_action_or_reset(dev, mt6785_power_off, dev);
	if (ret)
		return ret;
	regmap_write(afe->regmap, AFE_IRQ_MCU_EN, 3);
	regmap_write(afe->regmap, AFE_IRQ_MCU_SCP_EN, 0);
	regmap_write(afe->regmap, AFE_IRQ_MCU_CON0, 0);
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, 3);
	regmap_update_bits(afe->regmap, AFE_DL1_CON0,
			   DL1_NORMAL_MODE_MASK_SFT, DL1_NORMAL_MODE_MASK_SFT);
	regmap_update_bits(afe->regmap, AFE_VUL12_CON0,
			   VUL12_NORMAL_MODE_MASK_SFT | VUL12_4CH_EN_MASK_SFT,
			   VUL12_NORMAL_MODE_MASK_SFT);
	regmap_write(afe->regmap, AFE_CONN3, BIT(5));
	regmap_write(afe->regmap, AFE_CONN4, BIT(6));
	regmap_write(afe->regmap, AFE_CONN21, BIT(3));
	regmap_write(afe->regmap, AFE_CONN22, BIT(4));
	regmap_write(afe->regmap, AFE_CONN_24BIT, 0xffffffff);
	regmap_write(afe->regmap, AFE_CONN_24BIT_1, 0xffffffff);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(dev, irq, mt6785_interrupt, 0, dev_name(dev), afe);
	if (ret)
		return ret;
	ret = devm_snd_soc_register_component(dev, &mtk_afe_pcm_platform, NULL, 0);
	if (ret)
		return ret;
	return devm_snd_soc_register_component(dev, &mt6785_component,
					     mt6785_dais, ARRAY_SIZE(mt6785_dais));
}
