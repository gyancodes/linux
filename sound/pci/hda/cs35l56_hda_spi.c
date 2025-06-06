// SPDX-License-Identifier: GPL-2.0-only
//
// CS35L56 HDA audio driver SPI binding
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "cs35l56_hda.h"

static int cs35l56_hda_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct cs35l56_hda *cs35l56;
	int ret;

	cs35l56 = devm_kzalloc(&spi->dev, sizeof(*cs35l56), GFP_KERNEL);
	if (!cs35l56)
		return -ENOMEM;

	cs35l56->base.dev = &spi->dev;
	ret = cs35l56_init_config_for_spi(&cs35l56->base, spi);
	if (ret)
		return ret;

#ifdef CS35L56_WAKE_HOLD_TIME_US
	cs35l56->base.can_hibernate = true;
#endif

	cs35l56->base.fw_reg = &cs35l56_fw_reg;

	cs35l56->base.regmap = devm_regmap_init_spi(spi, &cs35l56_regmap_spi);
	if (IS_ERR(cs35l56->base.regmap)) {
		ret = PTR_ERR(cs35l56->base.regmap);
		dev_err(cs35l56->base.dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = cs35l56_hda_common_probe(cs35l56, id->driver_data, spi_get_chipselect(spi, 0));
	if (ret)
		return ret;
	ret = cs35l56_irq_request(&cs35l56->base, spi->irq);
	if (ret < 0)
		cs35l56_hda_remove(cs35l56->base.dev);

	return ret;
}

static void cs35l56_hda_spi_remove(struct spi_device *spi)
{
	cs35l56_hda_remove(&spi->dev);
}

static const struct spi_device_id cs35l56_hda_spi_id[] = {
	{ "cs35l54-hda", 0x3554 },
	{ "cs35l56-hda", 0x3556 },
	{ "cs35l57-hda", 0x3557 },
	{}
};

static const struct acpi_device_id cs35l56_acpi_hda_match[] = {
	{ "CSC3554", 0 },
	{ "CSC3556", 0 },
	{ "CSC3557", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, cs35l56_acpi_hda_match);

static struct spi_driver cs35l56_hda_spi_driver = {
	.driver = {
		.name		  = "cs35l56-hda",
		.acpi_match_table = cs35l56_acpi_hda_match,
		.pm		  = &cs35l56_hda_pm_ops,
	},
	.id_table	= cs35l56_hda_spi_id,
	.probe		= cs35l56_hda_spi_probe,
	.remove		= cs35l56_hda_spi_remove,
};
module_spi_driver(cs35l56_hda_spi_driver);

MODULE_DESCRIPTION("HDA CS35L56 SPI driver");
MODULE_IMPORT_NS("SND_HDA_SCODEC_CS35L56");
MODULE_IMPORT_NS("SND_SOC_CS35L56_SHARED");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
