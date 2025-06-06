// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX drm driver - parallel display implementation
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 */

#include <linux/component.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/bridge/imx.h>

#include "imx-drm.h"

struct imx_parallel_display_encoder {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct imx_parallel_display *pd;
};

struct imx_parallel_display {
	struct device *dev;
	u32 bus_format;
	struct drm_bridge *next_bridge;
};

static inline struct imx_parallel_display *bridge_to_imxpd(struct drm_bridge *b)
{
	return container_of(b, struct imx_parallel_display_encoder, bridge)->pd;
}

static const u32 imx_pd_bus_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_GBR888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB666_1X24_CPADHI,
	MEDIA_BUS_FMT_RGB565_1X16,
};

static u32 *
imx_pd_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					 struct drm_bridge_state *bridge_state,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state,
					 unsigned int *num_output_fmts)
{
	struct drm_display_info *di = &conn_state->connector->display_info;
	struct imx_parallel_display *imxpd = bridge_to_imxpd(bridge);
	u32 *output_fmts;

	if (!imxpd->bus_format && !di->num_bus_formats) {
		*num_output_fmts = ARRAY_SIZE(imx_pd_bus_fmts);
		return kmemdup(imx_pd_bus_fmts, sizeof(imx_pd_bus_fmts),
			       GFP_KERNEL);
	}

	*num_output_fmts = 1;
	output_fmts = kmalloc(sizeof(*output_fmts), GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	if (!imxpd->bus_format && di->num_bus_formats)
		output_fmts[0] = di->bus_formats[0];
	else
		output_fmts[0] = imxpd->bus_format;

	return output_fmts;
}

static bool imx_pd_format_supported(u32 output_fmt)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx_pd_bus_fmts); i++) {
		if (imx_pd_bus_fmts[i] == output_fmt)
			return true;
	}

	return false;
}

static u32 *
imx_pd_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts)
{
	struct imx_parallel_display *imxpd = bridge_to_imxpd(bridge);
	u32 *input_fmts;

	/*
	 * If the next bridge does not support bus format negotiation, let's
	 * use the static bus format definition (imxpd->bus_format) if it's
	 * specified, RGB888 when it's not.
	 */
	if (output_fmt == MEDIA_BUS_FMT_FIXED)
		output_fmt = imxpd->bus_format ? : MEDIA_BUS_FMT_RGB888_1X24;

	/* Now make sure the requested output format is supported. */
	if ((imxpd->bus_format && imxpd->bus_format != output_fmt) ||
	    !imx_pd_format_supported(output_fmt)) {
		*num_input_fmts = 0;
		return NULL;
	}

	*num_input_fmts = 1;
	input_fmts = kmalloc(sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	input_fmts[0] = output_fmt;
	return input_fmts;
}

static int imx_pd_bridge_atomic_check(struct drm_bridge *bridge,
				      struct drm_bridge_state *bridge_state,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);
	struct drm_display_info *di = &conn_state->connector->display_info;
	struct drm_bridge_state *next_bridge_state = NULL;
	struct drm_bridge *next_bridge;
	u32 bus_flags, bus_fmt;

	next_bridge = drm_bridge_get_next_bridge(bridge);
	if (next_bridge)
		next_bridge_state = drm_atomic_get_new_bridge_state(crtc_state->state,
								    next_bridge);

	if (next_bridge_state)
		bus_flags = next_bridge_state->input_bus_cfg.flags;
	else
		bus_flags = di->bus_flags;

	bus_fmt = bridge_state->input_bus_cfg.format;
	if (!imx_pd_format_supported(bus_fmt))
		return -EINVAL;

	bridge_state->output_bus_cfg.flags = bus_flags;
	bridge_state->input_bus_cfg.flags = bus_flags;
	imx_crtc_state->bus_flags = bus_flags;
	imx_crtc_state->bus_format = bridge_state->input_bus_cfg.format;
	imx_crtc_state->di_hsync_pin = 2;
	imx_crtc_state->di_vsync_pin = 3;

	return 0;
}

static int imx_pd_bridge_attach(struct drm_bridge *bridge,
				struct drm_encoder *encoder,
				enum drm_bridge_attach_flags flags)
{
	struct imx_parallel_display *imxpd = bridge_to_imxpd(bridge);

	return drm_bridge_attach(encoder, imxpd->next_bridge, bridge, flags);
}

static const struct drm_bridge_funcs imx_pd_bridge_funcs = {
	.attach = imx_pd_bridge_attach,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_check = imx_pd_bridge_atomic_check,
	.atomic_get_input_bus_fmts = imx_pd_bridge_atomic_get_input_bus_fmts,
	.atomic_get_output_bus_fmts = imx_pd_bridge_atomic_get_output_bus_fmts,
};

static int imx_pd_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct imx_parallel_display *imxpd = dev_get_drvdata(dev);
	struct imx_parallel_display_encoder *imxpd_encoder;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	int ret;

	imxpd_encoder = drmm_simple_encoder_alloc(drm, struct imx_parallel_display_encoder,
						  encoder, DRM_MODE_ENCODER_NONE);
	if (IS_ERR(imxpd_encoder))
		return PTR_ERR(imxpd_encoder);

	imxpd_encoder->pd = imxpd;
	encoder = &imxpd_encoder->encoder;
	bridge = &imxpd_encoder->bridge;

	ret = imx_drm_encoder_parse_of(drm, encoder, imxpd->dev->of_node);
	if (ret)
		return ret;

	bridge->funcs = &imx_pd_bridge_funcs;
	drm_bridge_attach(encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector))
		return PTR_ERR(connector);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static const struct component_ops imx_pd_ops = {
	.bind	= imx_pd_bind,
};

static int imx_pd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct imx_parallel_display *imxpd;
	int ret;
	u32 bus_format = 0;
	const char *fmt;

	imxpd = devm_kzalloc(dev, sizeof(*imxpd), GFP_KERNEL);
	if (!imxpd)
		return -ENOMEM;

	/* port@1 is the output port */
	imxpd->next_bridge = devm_drm_of_get_bridge(dev, np, 1, 0);
	if (imxpd->next_bridge == ERR_PTR(-ENODEV))
		imxpd->next_bridge = devm_imx_drm_legacy_bridge(dev, np, DRM_MODE_CONNECTOR_DPI);
	if (IS_ERR(imxpd->next_bridge)) {
		ret = PTR_ERR(imxpd->next_bridge);
		return ret;
	}

	ret = of_property_read_string(np, "interface-pix-fmt", &fmt);
	if (!ret) {
		if (!strcmp(fmt, "rgb24"))
			bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		else if (!strcmp(fmt, "rgb565"))
			bus_format = MEDIA_BUS_FMT_RGB565_1X16;
		else if (!strcmp(fmt, "bgr666"))
			bus_format = MEDIA_BUS_FMT_RGB666_1X18;
		else if (!strcmp(fmt, "lvds666"))
			bus_format = MEDIA_BUS_FMT_RGB666_1X24_CPADHI;
	}
	imxpd->bus_format = bus_format;

	imxpd->dev = dev;

	platform_set_drvdata(pdev, imxpd);

	return component_add(dev, &imx_pd_ops);
}

static void imx_pd_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_pd_ops);
}

static const struct of_device_id imx_pd_dt_ids[] = {
	{ .compatible = "fsl,imx-parallel-display", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_pd_dt_ids);

static struct platform_driver imx_pd_driver = {
	.probe		= imx_pd_probe,
	.remove		= imx_pd_remove,
	.driver		= {
		.of_match_table = imx_pd_dt_ids,
		.name	= "imx-parallel-display",
	},
};

module_platform_driver(imx_pd_driver);

MODULE_DESCRIPTION("i.MX parallel display driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-parallel-display");
