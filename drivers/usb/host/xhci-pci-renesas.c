// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas uPD720201/uPD720202 PCIe xHCI controller driver.
 *
 * This controller has no firmware in ROM; a firmware image must be
 * downloaded over PCI configuration space before the device behaves as a
 * standard xHCI host controller. Ported from the Linux kernel driver
 * drivers/usb/host/xhci-pci-renesas.c (internal-RAM download path only -
 * this board has no external SPI ROM strapped to the controller).
 *
 * Copyright (c) Qualcomm Technologies, Inc. All rights reserved.
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <init.h>
#include <log.h>
#include <pci.h>
#include <pci_ids.h>
#include <reset.h>
#include <time.h>
#include <usb.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <usb/xhci.h>

#include "xhci-pci-renesas-fw.h"

#define RENESAS_FW_VERSION		0x6c
#define RENESAS_FW_VERSION_FIELD	GENMASK(23, 7)
#define RENESAS_FW_VERSION_OFFSET	8

#define RENESAS_FW_STATUS		0xf4
#define RENESAS_FW_STATUS_DOWNLOAD_ENABLE	BIT(0)
#define RENESAS_FW_STATUS_LOCK			BIT(1)
#define RENESAS_FW_STATUS_RESULT		GENMASK(6, 4)
#define RENESAS_FW_STATUS_INVALID		0
#define RENESAS_FW_STATUS_SUCCESS		BIT(4)
#define RENESAS_FW_STATUS_ERROR		BIT(5)

#define RENESAS_FW_STATUS_MSB		0xf5
#define RENESAS_FW_STATUS_SET_DATA0	BIT(0)
#define RENESAS_FW_STATUS_SET_DATA1	BIT(1)

#define RENESAS_DATA0			0xf8
#define RENESAS_DATA1			0xfc

/* Firmware header, per Renesas datasheet R19UH0078EJ0500 Rev.5.00 6.3 */
#define RENESAS_FW_MIN_SIZE		0x1000
#define RENESAS_FW_MAX_SIZE		0x10000
#define RENESAS_FW_MAGIC		0x55aa

#define RENESAS_RETRY			50000	/* * 10us ~= 500ms */
#define RENESAS_DELAY_US		10

struct xhci_pci_renesas_plat {
	struct reset_ctl reset;
};

/*
 * renesas_fw_verify() - sanity-check the embedded firmware header
 *
 * @data: firmware image, treated as an array of 32-bit little-endian words
 * @size: firmware image size in bytes
 */
static int renesas_fw_verify(const u32 *data, size_t size)
{
	const u8 *fw_data = (const u8 *)data;
	u16 fw_version_pointer;

	if (size < RENESAS_FW_MIN_SIZE || size >= RENESAS_FW_MAX_SIZE) {
		log_err("renesas xhci: bad firmware size %zu\n", size);
		return -EINVAL;
	}

	if (get_unaligned_le16(fw_data) != RENESAS_FW_MAGIC) {
		log_err("renesas xhci: bad firmware magic\n");
		return -EINVAL;
	}

	fw_version_pointer = get_unaligned_le16(fw_data + 4);
	if (fw_version_pointer + 2 >= size) {
		log_err("renesas xhci: bad firmware version pointer\n");
		return -EINVAL;
	}

	log_err("renesas xhci: firmware version %02x.%02x\n",
		fw_data[fw_version_pointer + 1], fw_data[fw_version_pointer]);

	return 0;
}

/*
 * renesas_fw_check_running() - check whether firmware is already loaded
 *
 * Return: 0 if firmware is already loaded and running, 1 if firmware needs
 * to be downloaded, or a negative error code on unrecoverable failure.
 */
static int renesas_fw_check_running(struct udevice *dev)
{
	u8 fw_status;
	int ret;

	ret = dm_pci_read_config8(dev, RENESAS_FW_STATUS, &fw_status);
	if (ret)
		return ret;

	if (fw_status & RENESAS_FW_STATUS_LOCK) {
		if (fw_status & RENESAS_FW_STATUS_SUCCESS)
			return 0;

		dev_err(dev, "renesas xhci: firmware lock set without success\n");
		return -EIO;
	}

	if (fw_status & RENESAS_FW_STATUS_DOWNLOAD_ENABLE) {
		dev_err(dev, "renesas xhci: firmware download left enabled\n");
		return -EIO;
	}

	switch (fw_status & RENESAS_FW_STATUS_RESULT) {
	case RENESAS_FW_STATUS_INVALID:
		return 1;
	case RENESAS_FW_STATUS_SUCCESS:
		return 0;
	case RENESAS_FW_STATUS_ERROR:
		return -ENODEV;
	default:
		return -EINVAL;
	}
}

/*
 * renesas_fw_download_word() - transfer a single 32-bit firmware word
 *
 * @step: word index into the firmware image; even steps go to DATA0, odd
 * steps go to DATA1, per the controller's 64-bit "row" mailbox layout.
 */
static int renesas_fw_download_word(struct udevice *dev, u32 word, size_t step)
{
	bool data1 = step & 1;
	u8 status_bit = data1 ? RENESAS_FW_STATUS_SET_DATA1 :
				 RENESAS_FW_STATUS_SET_DATA0;
	u8 fw_status;
	int ret, i;

	for (i = 0; i < RENESAS_RETRY; i++) {
		ret = dm_pci_read_config8(dev, RENESAS_FW_STATUS_MSB, &fw_status);
		if (ret)
			return ret;
		if (!(fw_status & status_bit))
			break;
		udelay(RENESAS_DELAY_US);
	}
	if (i == RENESAS_RETRY)
		return -ETIMEDOUT;

	ret = dm_pci_write_config32(dev, data1 ? RENESAS_DATA1 : RENESAS_DATA0,
				    word);
	if (ret)
		return ret;
	udelay(100);

	return dm_pci_write_config8(dev, RENESAS_FW_STATUS_MSB, status_bit);
}

/*
 * renesas_fw_download() - download firmware into the controller's internal
 * RAM over PCI configuration space
 */
static int renesas_fw_download(struct udevice *dev, const u32 *fw_data,
			       size_t fw_size)
{
	u8 fw_status;
	int ret, i;
	size_t words = fw_size / 4;

	ret = dm_pci_write_config8(dev, RENESAS_FW_STATUS,
				   RENESAS_FW_STATUS_DOWNLOAD_ENABLE);
	if (ret)
		return ret;

	for (i = 0; i < words; i++) {
		ret = renesas_fw_download_word(dev, fw_data[i], i);
		if (ret) {
			dev_err(dev, "renesas xhci: download failed at word %d/%zu (%d)\n",
				i, words, ret);
			return ret;
		}
	}

	for (i = 0; i < RENESAS_RETRY; i++) {
		ret = dm_pci_read_config8(dev, RENESAS_FW_STATUS_MSB, &fw_status);
		if (ret)
			return ret;
		if (!(fw_status & (RENESAS_FW_STATUS_SET_DATA0 |
				    RENESAS_FW_STATUS_SET_DATA1)))
			break;
		udelay(RENESAS_DELAY_US);
	}
	if (i == RENESAS_RETRY)
		dev_warn(dev, "renesas xhci: timeout waiting for final word ack\n");

	ret = dm_pci_write_config8(dev, RENESAS_FW_STATUS, 0);
	if (ret)
		return ret;

	for (i = 0; i < RENESAS_RETRY; i++) {
		ret = dm_pci_read_config8(dev, RENESAS_FW_STATUS, &fw_status);
		if (ret)
			return ret;
		if (fw_status & RENESAS_FW_STATUS_SUCCESS)
			break;
		if (fw_status & RENESAS_FW_STATUS_ERROR) {
			dev_err(dev, "renesas xhci: firmware download error\n");
			return -EIO;
		}
		udelay(RENESAS_DELAY_US);
	}
	if (i == RENESAS_RETRY) {
		dev_err(dev, "renesas xhci: timeout waiting for download result\n");
		return -ETIMEDOUT;
	}

	log_err("renesas xhci: firmware download succeeded\n");
	return 0;
}

static int renesas_xhci_check_request_fw(struct udevice *dev)
{
	int ret;

	ret = renesas_fw_check_running(dev);
	if (ret <= 0)
		return ret;

	ret = renesas_fw_verify(renesas_fw_data, RENESAS_FW_SIZE * 4);
	if (ret)
		return ret;

	log_err("renesas xhci: firmware not running, downloading\n");
	return renesas_fw_download(dev, renesas_fw_data, RENESAS_FW_SIZE * 4);
}

static int xhci_pci_renesas_init(struct udevice *dev,
				 struct xhci_hccr **ret_hccr,
				 struct xhci_hcor **ret_hcor)
{
	struct xhci_hccr *hccr;
	struct xhci_hcor *hcor;
	u32 cmd;

	hccr = (struct xhci_hccr *)dm_pci_map_bar(dev, PCI_BASE_ADDRESS_0, 0,
						   0, PCI_REGION_TYPE,
						   PCI_REGION_MEM);
	if (!hccr) {
		dev_err(dev, "renesas xhci: cannot map PCI mem bar\n");
		return -EIO;
	}

	hcor = (struct xhci_hcor *)((uintptr_t)hccr +
			HC_LENGTH(xhci_readl(&hccr->cr_capbase)));

	log_err("renesas xhci: hccr %p hcor %p\n", hccr, hcor);

	*ret_hccr = hccr;
	*ret_hcor = hcor;

	dm_pci_read_config32(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MASTER;
	dm_pci_write_config32(dev, PCI_COMMAND, cmd);

	return 0;
}

static int xhci_pci_renesas_probe(struct udevice *dev)
{
	struct xhci_pci_renesas_plat *plat = dev_get_plat(dev);
	struct xhci_ctrl *ctrl = dev_get_priv(dev);
	struct xhci_hccr *hccr;
	struct xhci_hcor *hcor;
	int ret;

	ret = reset_get_by_index(dev, 0, &plat->reset);
	if (ret && ret != -ENOENT && ret != -ENOTSUPP) {
		dev_err(dev, "renesas xhci: failed to get reset\n");
		return ret;
	}

	if (reset_valid(&plat->reset)) {
		ret = reset_assert(&plat->reset);
		if (ret)
			goto err_reset;

		ret = reset_deassert(&plat->reset);
		if (ret)
			goto err_reset;
	}

	ret = renesas_xhci_check_request_fw(dev);
	if (ret) {
		dev_err(dev, "renesas xhci: firmware bring-up failed (%d)\n", ret);
		goto err_reset;
	}

	/*
	 * Settle time between firmware-download-success and first MMIO/xHCI
	 * touch. The Renesas on-chip CPU needs time to finish booting into
	 * xHCI mode after FW_STATUS reports SUCCESS.
	 */
	mdelay(100);

	ret = xhci_pci_renesas_init(dev, &hccr, &hcor);
	if (ret)
		goto err_reset;

	ret = xhci_register(dev, hccr, hcor);
	if (ret)
		goto err_reset;

	return 0;

err_reset:
	if (reset_valid(&plat->reset))
		reset_free(&plat->reset);

	return ret;
}

static int xhci_pci_renesas_remove(struct udevice *dev)
{
	struct xhci_pci_renesas_plat *plat = dev_get_plat(dev);

	xhci_deregister(dev);
	if (reset_valid(&plat->reset))
		reset_free(&plat->reset);

	return 0;
}

/*
 * pci_find_and_bind_driver() binds the first alphabetically-sorted driver
 * whose pci_device_id table matches. The uPD720201 reports a standard xHCI
 * class code even before firmware loads, matching generic xhci-pci.c's
 * wildcard too, so "renesas_xhci_pci" relies on sorting before "xhci_pci" -
 * do not rename without re-verifying bind order on hardware.
 */
U_BOOT_DRIVER(renesas_xhci_pci) = {
	.name	= "renesas_xhci_pci",
	.id	= UCLASS_USB,
	.probe = xhci_pci_renesas_probe,
	.remove	= xhci_pci_renesas_remove,
	.ops	= &xhci_usb_ops,
	.plat_auto	= sizeof(struct xhci_pci_renesas_plat),
	.priv_auto	= sizeof(struct xhci_ctrl),
	.flags	= DM_FLAG_OS_PREPARE | DM_FLAG_ALLOC_PRIV_DMA,
};

static struct pci_device_id xhci_pci_renesas_supported[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_UPD720201) },
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_UPD720202) },
	{},
};

U_BOOT_PCI_DEVICE(renesas_xhci_pci, xhci_pci_renesas_supported);
