/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __MT6785_AFE_H
#define __MT6785_AFE_H

#include <linux/clk.h>
#include "../common/mtk-base-afe.h"

struct mt6785_afe {
	struct clk_bulk_data clocks[6];
	struct clk_bulk_data speaker_clocks[6];
	struct regmap *topckgen;
};

int mt6785_afe_probe(struct platform_device *pdev);
extern const struct dev_pm_ops mt6785_afe_pm_ops;

#endif
