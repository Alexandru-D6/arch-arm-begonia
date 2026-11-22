// SPDX-License-Identifier: GPL-2.0-only
/* NXP TFA9874 speaker path for Xiaomi begonia.
 * Silicon initialization follows Xiaomi's GPL tfa98xx/src/tfa_init.c,
 * Copyright (C) 2014-2019 NXP Semiconductors. Board fields come from the
 * handset's own stock profile, rather than tuning for a different speaker.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

#define TFA_SYSTEM 0x00
#define TFA_MANAGER 0x01
#define TFA_REVISION 0x03
#define TFA_STATUS1 0x11
#define TFA_PWDN BIT(0)
#define TFA_AMPE BIT(3)
#define TFA_BOOST BIT(4)

struct tfa9874_field { u16 field, value; };
#include "tfa9874-begonia-profile.h"

struct begonia_tfa {
	struct regmap *regmap;
	struct device *dev;
};

static const struct reg_sequence tfa_0c74_init[] = {
	{ 0x02, 0x22c8 }, { 0x52, 0x57dc }, { 0x53, 0x003e },
	{ 0x56, 0x0400 }, { 0x61, 0x0110 }, { 0x6f, 0x00a5 },
	{ 0x70, 0x07f8 }, { 0x73, 0x0047 }, { 0x74, 0x5098 },
	{ 0x75, 0x8d28 }, { 0x80, 0x0000 }, { 0x83, 0x0799 },
	{ 0x84, 0x0081 },
};

static int begonia_tfa_off(struct begonia_tfa *tfa)
{
	int ret;

	ret = regmap_update_bits(tfa->regmap, TFA_SYSTEM,
				TFA_AMPE | TFA_BOOST, 0);
	if (ret)
		return ret;
	return regmap_update_bits(tfa->regmap, TFA_SYSTEM, TFA_PWDN, TFA_PWDN);
}

static void begonia_tfa_shutdown(void *data)
{
	begonia_tfa_off(data);
}

static int begonia_tfa_event(struct snd_soc_dapm_widget *widget,
			     struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(widget->dapm);
	struct begonia_tfa *tfa = snd_soc_component_get_drvdata(component);
	unsigned int status = 0;
	int ret;

	if (event == SND_SOC_DAPM_PRE_PMD)
		return begonia_tfa_off(tfa);
	/* Enable the output stage only after the serial clocks have locked. */
	ret = regmap_update_bits(tfa->regmap, TFA_MANAGER, BIT(2), BIT(2));
	if (ret)
		goto fail;
	ret = regmap_update_bits(tfa->regmap, TFA_SYSTEM,
				TFA_PWDN | TFA_AMPE | TFA_BOOST, TFA_BOOST);
	if (ret)
		goto fail;
	ret = regmap_read_poll_timeout(tfa->regmap, TFA_STATUS1, status,
				      (status & (BIT(6) | BIT(7))) == (BIT(6) | BIT(7)),
				      500, 30000);
	if (ret) {
		dev_err(tfa->dev, "speaker clocks not locked, status %#x\n", status);
		goto fail;
	}
	ret = regmap_update_bits(tfa->regmap, TFA_SYSTEM, TFA_AMPE, TFA_AMPE);
	if (!ret)
		return 0;
fail:
	begonia_tfa_off(tfa);
	return ret;
}

static const struct snd_soc_dapm_widget begonia_tfa_widgets[] = {
	SND_SOC_DAPM_AIF_IN("Speaker AIF", "Speaker Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUT_DRV_E("Speaker Amplifier", SND_SOC_NOPM, 0, 0, NULL, 0,
			     begonia_tfa_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("Speaker OUT"),
};
static const struct snd_soc_dapm_route begonia_tfa_routes[] = {
	{ "Speaker OUT", NULL, "Speaker Amplifier" },
	{ "Speaker Amplifier", NULL, "Speaker AIF" },
};

/* Bound the hardware gain to the factory profile maximum (0xbe). */
static const struct snd_kcontrol_new begonia_tfa_controls[] = {
	SOC_SINGLE("Speaker Playback Volume", 0x52, 5, 0xbe, 0),
};

static int begonia_tfa_component_probe(struct snd_soc_component *component)
{
	struct begonia_tfa *tfa = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, tfa->regmap);
	return 0;
}
static const struct snd_soc_component_driver begonia_tfa_component = {
	.name = "begonia-tfa9874",
	.probe = begonia_tfa_component_probe,
	.dapm_widgets = begonia_tfa_widgets,
	.num_dapm_widgets = ARRAY_SIZE(begonia_tfa_widgets),
	.dapm_routes = begonia_tfa_routes,
	.num_dapm_routes = ARRAY_SIZE(begonia_tfa_routes),
	.controls = begonia_tfa_controls,
	.num_controls = ARRAY_SIZE(begonia_tfa_controls),
	.endianness = 1,
};
static struct snd_soc_dai_driver begonia_tfa_dai = {
	.name = "begonia-tfa9874-aif",
	.playback = {
		.stream_name = "Speaker Playback", .channels_min = 1, .channels_max = 1,
		.rates = SNDRV_PCM_RATE_48000, .formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};
static const struct regmap_config begonia_tfa_regmap = {
	.reg_bits = 8, .val_bits = 16, .max_register = 0xfb,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_NONE,
};

static int begonia_tfa_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct begonia_tfa *tfa;
	struct gpio_desc *reset;
	unsigned int revision, key;
	int i, ret;

	ret = devm_regulator_get_enable(dev, "vddd");
	if (ret)
		return dev_err_probe(dev, ret, "speaker digital supply\n");
	reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "speaker reset\n");
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(reset, 0);
	usleep_range(5000, 6000);
	tfa = devm_kzalloc(dev, sizeof(*tfa), GFP_KERNEL);
	if (!tfa)
		return -ENOMEM;
	tfa->dev = dev;
	tfa->regmap = devm_regmap_init_i2c(client, &begonia_tfa_regmap);
	if (IS_ERR(tfa->regmap))
		return PTR_ERR(tfa->regmap);
	i2c_set_clientdata(client, tfa);
	ret = regmap_read(tfa->regmap, TFA_REVISION, &revision);
	if (ret)
		return dev_err_probe(dev, ret, "speaker revision\n");
	/* The register sequence below is specific to this silicon revision. */
	if (revision != 0x0c74)
		return dev_err_probe(dev, -ENODEV, "unsupported speaker revision %#x\n", revision);
	ret = devm_add_action_or_reset(dev, begonia_tfa_shutdown, tfa);
	if (ret)
		return ret;
	ret = begonia_tfa_off(tfa);
	if (ret)
		return ret;
	ret = regmap_write(tfa->regmap, 0x0f, 0x5a6b);
	if (ret)
		return ret;
	ret = regmap_read(tfa->regmap, 0xfb, &key);
	if (ret)
		return ret;
	ret = regmap_write(tfa->regmap, 0xa0, key ^ 0x005a);
	if (ret)
		return ret;
	ret = regmap_write(tfa->regmap, 0x0f, 0);
	if (ret)
		return ret;
	ret = regmap_multi_reg_write(tfa->regmap, tfa_0c74_init, ARRAY_SIZE(tfa_0c74_init));
	if (ret)
		return ret;
	ret = regmap_multi_reg_write(tfa->regmap, begonia_tfa_registers,
				     ARRAY_SIZE(begonia_tfa_registers));
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(begonia_tfa_fields); i++) {
		u16 field = begonia_tfa_fields[i].field;
		u8 shift = (field >> 4) & 0xf;
		u8 width = (field & 0xf) + 1;

		ret = regmap_update_bits(tfa->regmap, field >> 8,
					GENMASK(shift + width - 1, shift),
					begonia_tfa_fields[i].value << shift);
		if (ret)
			return ret;
	}
	ret = regmap_update_bits(tfa->regmap, 0x20, BIT(4), BIT(4));
	if (ret)
		return ret;
	/* Start below the stock maximum. ALSA/PipeWire can adjust the volume. */
	ret = regmap_update_bits(tfa->regmap, 0x52, GENMASK(12, 5), 0x8e << 5);
	if (ret)
		return ret;
	ret = begonia_tfa_off(tfa);
	if (ret)
		return ret;
	dev_info(dev, "TFA9874 %#x, begonia speaker profile ready\n", revision);
	return devm_snd_soc_register_component(dev, &begonia_tfa_component, &begonia_tfa_dai, 1);
}
static const struct of_device_id begonia_tfa_match[] = {
	{ .compatible = "xiaomi,begonia-tfa9874" }, { }
};
MODULE_DEVICE_TABLE(of, begonia_tfa_match);
static struct i2c_driver begonia_tfa_driver = {
	.probe = begonia_tfa_probe,
	.driver = { .name = "begonia-tfa9874", .of_match_table = begonia_tfa_match },
};
module_i2c_driver(begonia_tfa_driver);
MODULE_DESCRIPTION("Begonia TFA9874 internal speaker");
MODULE_LICENSE("GPL");
