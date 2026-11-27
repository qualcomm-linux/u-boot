/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Polled GPI (GSI DMA) backend for the GENI I2C driver.
 *
 * Used when the serial engine has been handed over in GSI mode
 * (FIFO_IF_DISABLE=1) and the FIFO interface can't be re-enabled from the
 * non-secure world.
 */

#ifndef __GENI_I2C_GPI_H
#define __GENI_I2C_GPI_H

#include <linux/errno.h>
#include <linux/types.h>

struct udevice;
struct i2c_msg;
struct geni_gpi;

/* Bus timing programmed through the I2C CONFIG0 TRE */
struct geni_gpi_timing {
	u8 clk_div;
	u8 t_high_cnt;
	u8 t_low_cnt;
	u8 t_cycle_cnt;
};

#if IS_ENABLED(CONFIG_SYS_I2C_GENI_GPI)
/**
 * geni_gpi_init() - switch the SE to GPI mode and bring up its GPII
 *
 * @dev: the I2C controller (its "dmas" property selects the GPI controller
 *       and the SE id)
 * @se_base: base address of the serial engine
 * @gpip: returns the GPI context
 * Return: 0 on success, -ve errno on failure
 */
int geni_gpi_init(struct udevice *dev, phys_addr_t se_base, struct geni_gpi **gpip);

/**
 * geni_gpi_xfer() - run an I2C transfer through the GPI channels
 *
 * Return: 0 on success, -ve errno on failure
 */
int geni_gpi_xfer(struct geni_gpi *gpi, struct i2c_msg *msgs, int num,
		  const struct geni_gpi_timing *timing);

/** geni_gpi_remove() - stop and de-allocate the GPII so the OS can claim it */
void geni_gpi_remove(struct geni_gpi *gpi);
#else
static inline int geni_gpi_init(struct udevice *dev, phys_addr_t se_base,
				struct geni_gpi **gpip)
{
	return -ENOSYS;
}

static inline int geni_gpi_xfer(struct geni_gpi *gpi, struct i2c_msg *msgs,
				int num, const struct geni_gpi_timing *timing)
{
	return -ENOSYS;
}

static inline void geni_gpi_remove(struct geni_gpi *gpi) {}
#endif

#endif /* __GENI_I2C_GPI_H */
