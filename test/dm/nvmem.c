// SPDX-License-Identifier: GPL-2.0
/*
 * Test for NVMEM bit field support
 */

#include <dm.h>
#include <i2c_eeprom.h>
#include <nvmem.h>
#include <dm/test.h>
#include <test/test.h>
#include <test/ut.h>

static int nvmem_test_write_raw(struct udevice *dev, uint offset,
				const void *buf, uint size)
{
	return i2c_eeprom_write(dev, offset, buf, size);
}

static int nvmem_test_read_raw(struct udevice *dev, uint offset,
			       void *buf, uint size)
{
	return i2c_eeprom_read(dev, offset, buf, size);
}

/* Test NVMEM bit field operations */
static int dm_test_nvmem_bitfield(struct unit_test_state *uts)
{
	struct udevice *nvmem_dev;
	struct nvmem_cell cell;
	u32 value;
	u8 hw_value_u8;
	u32 hw_value_u32;

	ut_assertok(uclass_get_device_by_name(UCLASS_I2C_EEPROM,
					      "nvmem-test@50", &nvmem_dev));

	cell.nvmem = nvmem_dev;

	/* Test reg = <0x0 0x1>; bits = <1 7>: */
	cell.offset = 0x0;
	cell.size = 1;
	cell.bit_offset = 1;
	cell.nbits = 7;
	hw_value_u8 = 0x01;
	ut_assertok(nvmem_test_write_raw(nvmem_dev, cell.offset, &hw_value_u8, 1));
	value = 0x7f;
	ut_assertok(nvmem_cell_write(&cell, &value, sizeof(value)));
	value = 0;
	ut_assertok(nvmem_cell_read(&cell, &value, sizeof(value)));
	ut_asserteq(0x7f, value);
	ut_assertok(nvmem_test_read_raw(nvmem_dev, cell.offset, &hw_value_u8, 1));
	ut_asserteq(0xff, hw_value_u8);

	/* Test reg = <0x18 0x4>; bits = <4 12>: Spanning byte boundary */
	cell.offset = 0x18;
	cell.size = 4;
	cell.bit_offset = 4;
	cell.nbits = 12;
	hw_value_u32 = 0x0000000f;
	ut_assertok(nvmem_test_write_raw(nvmem_dev, cell.offset, (u8 *)&hw_value_u32, 4));
	value = 0xfff;
	ut_assertok(nvmem_cell_write(&cell, &value, sizeof(value)));
	value = 0;
	ut_assertok(nvmem_cell_read(&cell, &value, sizeof(value)));
	ut_asserteq(0xfff, value);
	ut_assertok(nvmem_test_read_raw(nvmem_dev, cell.offset, (u8 *)&hw_value_u32, 4));
	ut_asserteq(0x0000ffff, hw_value_u32);

	/* Test reg = <0x9 0x4>: Full 4-byte access without bit field */
	cell.offset = 0x9;
	cell.bit_offset = 0;
	cell.nbits = 0;
	value = 0x12345678;
	ut_assertok(nvmem_cell_write(&cell, &value, sizeof(value)));
	value = 0;
	ut_assertok(nvmem_cell_read(&cell, &value, sizeof(value)));
	ut_asserteq(0x12345678, value);

	/* Test reg = <0xc 0x4>; bits = <16 16>: Upper 2 bytes */
	cell.offset = 0xc;
	cell.bit_offset = 16;
	cell.nbits = 16;
	hw_value_u32 = 0x0000ffff;
	ut_assertok(nvmem_test_write_raw(nvmem_dev, cell.offset, (u8 *)&hw_value_u32, 4));
	value = 0xffff;
	ut_assertok(nvmem_cell_write(&cell, &value, sizeof(value)));
	value = 0;
	ut_assertok(nvmem_cell_read(&cell, &value, sizeof(value)));
	ut_asserteq(0xffff, value);
	ut_assertok(nvmem_test_read_raw(nvmem_dev, cell.offset, (u8 *)&hw_value_u32, 4));
	ut_asserteq(0xffffffff, hw_value_u32);

	return 0;
}
DM_TEST(dm_test_nvmem_bitfield,
	UTF_PROBE_TEST | UTF_SCAN_FDT | UTF_FLAT_TREE);

/* Test NVMEM error handling for invalid configurations */
static int dm_test_nvmem_bitfield_errors(struct unit_test_state *uts)
{
	struct udevice *nvmem_dev;
	struct nvmem_cell cell;
	u32 value;
	int ret;

	ut_assertok(uclass_get_device_by_name(UCLASS_I2C_EEPROM,
					      "nvmem-test@50", &nvmem_dev));

	/* Test bit field exceeding cell size */
	cell.nvmem = nvmem_dev;
	cell.offset = 0xd;
	cell.size = 1;
	cell.bit_offset = 0;
	cell.nbits = 9;

	value = 0xff;
	ret = nvmem_cell_write(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	ret = nvmem_cell_read(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	/* Test bit field exceeding 32 bits */
	cell.size = 4;
	cell.bit_offset = 0;
	cell.nbits = 33;

	ret = nvmem_cell_write(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	ret = nvmem_cell_read(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	/* Test invalid bit_offset + nbits */
	cell.size = 1;
	cell.bit_offset = 7;
	cell.nbits = 2;

	ret = nvmem_cell_write(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	ret = nvmem_cell_read(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	/* Test nbits=0 requires buffer size == cell size */
	cell.size = 1;
	cell.bit_offset = 0;
	cell.nbits = 0;

	value = 0xff;
	ret = nvmem_cell_write(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	ret = nvmem_cell_read(&cell, &value, sizeof(value));
	ut_asserteq(-EINVAL, ret);

	return 0;
}
DM_TEST(dm_test_nvmem_bitfield_errors,
	UTF_PROBE_TEST | UTF_SCAN_FDT | UTF_FLAT_TREE);
