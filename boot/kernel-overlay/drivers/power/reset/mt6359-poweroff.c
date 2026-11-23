// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power off through the MT6359 PMIC's PWRHOLD signal.
 *
 * The stock begonia kernel clears PPCCTL0.RG_PWRHOLD from
 * rtc_bbpu_pwrdown(). PSCI SYSTEM_OFF alone leaves this board requiring
 * a long hardware-reset press before it can boot again.
 */
#include <linux/delay.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#define MT6359_PPCCTL0		0x0a08
#define MT6359_RG_PWRHOLD		BIT(0)

struct mt6359_pwrc {
	struct device *dev;
	struct regmap *regmap;
};

static int mt6359_poweroff(struct sys_off_data *data)
{
	struct mt6359_pwrc *pwrc = data->cb_data;
	int ret;

	dev_info(pwrc->dev, "clearing PWRHOLD to power off\n");
	ret = regmap_update_bits(pwrc->regmap, MT6359_PPCCTL0,
				 MT6359_RG_PWRHOLD, 0);
	if (!ret)
		msleep(1000);

	/* Leave the firmware power-off handler available if this fails. */
	dev_err(pwrc->dev, "power-off failed (%d); falling back to firmware\n", ret);
	return NOTIFY_DONE;
}

static int mt6359_pwrc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt6397_chip *chip = dev_get_drvdata(dev->parent);
	struct mt6359_pwrc *pwrc;
	int ret;

	/* Only claim power-off when explicitly enabled in the board DT. */
	if (!dev->of_node || !chip || chip->chip_id != MT6359_CHIP_ID)
		return -ENODEV;

	pwrc = devm_kzalloc(dev, sizeof(*pwrc), GFP_KERNEL);
	if (!pwrc)
		return -ENOMEM;
	pwrc->dev = dev;
	pwrc->regmap = chip->regmap;

	/*
	 * PWRAP regmap accesses can sleep. POWER_OFF_PREPARE runs after
	 * device_shutdown(), with interrupts and the scheduler still enabled.
	 * Filesystems have already been synced and unmounted by userspace.
	 * Do not use the atomic POWER_OFF chain for this bus.
	 */
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF_PREPARE,
					    SYS_OFF_PRIO_DEFAULT, mt6359_poweroff,
					    pwrc);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register power-off handler\n");

	dev_info(dev, "registered MT6359 PWRHOLD power-off handler\n");
	return 0;
}

static const struct of_device_id mt6359_pwrc_of_match[] = {
	{ .compatible = "mediatek,mt6359-pwrc" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6359_pwrc_of_match);

static struct platform_driver mt6359_pwrc_driver = {
	.probe = mt6359_pwrc_probe,
	.driver = {
		.name = "mt6359-pwrc",
		.of_match_table = mt6359_pwrc_of_match,
	},
};
module_platform_driver(mt6359_pwrc_driver);

MODULE_DESCRIPTION("MediaTek MT6359 PMIC power-off driver");
MODULE_LICENSE("GPL");
