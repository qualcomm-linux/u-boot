// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm TrustZone Peripheral Image Loader (PIL) support for the QUPv3
 * GSI firmware.
 *
 * Some GENI serial-engine instances are marked qcom,shared-se in the
 * devicetree -- their firmware RAM is owned by TrustZone and left
 * XPU-locked by the boot ROM until something authenticates and unlocks
 * it. Qualcomm's EDK2/ABL bootloader does this in GpiDxe -> gpi_init() ->
 * gpi_init_all() -> gpi_load_firmware() (GpiDrvLib/src/gpi_fwload.c)
 * before Linux boots. U-Boot has no equivalent, so on boards with such a
 * shared SE the block is left exactly as reset left it, and whichever
 * other execution environment also uses that SE (e.g. the modem) faults
 * the first time it touches the block.
 *
 * This mirrors gpi_load_firmware(): read the signed "qupfw" ELF, split it
 * into TZ's expected metadata/program-segment layout, and run it through
 * the same four PIL SCM calls EDK2 uses (init -> mem_setup ->
 * auth_and_reset -> unlock_xpu). The call sequence and parameter encoding
 * match Linux's qcom_scm_pas_init_image()/_mem_setup()/_auth_and_reset()/
 * _shutdown() (drivers/firmware/qcom/qcom_scm.c) -- same QCOM_SCM_SVC_PIL
 * service, same command numbers (0x01/0x02/0x05/0x06) -- just invoked
 * here for the QUP GSI block instead of a DSP.
 *
 * The buffers handed to TZ must be 4 KiB aligned, not just cacheline
 * aligned: TZ XPU-locks them at 4 KiB granularity, and a cacheline-only
 * aligned buffer can still land anywhere within a page.
 *
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <blk.h>
#include <part.h>
#include <cpu_func.h>
#include <elf.h>
#include <dm/uclass.h>
#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <log.h>
#include <malloc.h>
#include <soc/qcom/qup-fw-load.h>

#include "qcom_scm_pil.h"

#define TZ_SVC_PIL		0x02

#define TZ_PIL_INIT_ID		0x01
#define TZ_PIL_MEM_ID		0x02
#define TZ_PIL_AUTH_RESET_ID	0x05
#define TZ_PIL_UNLOCK_XPU_ID	0x06

/*
 * TZ reads buffers handed to PIL calls directly and XPU-locks them at
 * 4 KiB granularity -- the base (and, for the program segment, both ends)
 * must be page aligned, not just cacheline aligned.
 */
#define QUPFW_ALIGN		SZ_4K

static u32 scm_fnid(u32 svc, u32 cmd)
{
	return ((svc & 0xff) << 8) | (cmd & 0xff);
}

static u32 scm_arginfo(u32 nargs, u32 t0, u32 t1, u32 t2)
{
	/* arg type: 0 = VAL, 2 = RW buffer -- matches QCOM_SCM_VAL/_RW */
	return (nargs & 0xf) | ((t0 & 0x3) << 4) | ((t1 & 0x3) << 6) |
	       ((t2 & 0x3) << 8);
}

static int scm_pil_call(u32 cmd, u32 arginfo, unsigned long a2,
			 unsigned long a3, unsigned long a4)
{
	struct arm_smccc_res res;
	u32 fnid = scm_fnid(TZ_SVC_PIL, cmd);
	unsigned long smc_id = ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,
						   ARM_SMCCC_SMC_64,
						   ARM_SMCCC_OWNER_SIP, fnid);

	arm_smccc_smc(smc_id, arginfo, a2, a3, a4, 0, 0, 0, &res);
	debug("qcom_scm_pil: cmd 0x%x arginfo 0x%x -> 0x%lx\n",
	      cmd, arginfo, (unsigned long)res.a0);

	if (res.a0)
		return -EIO;

	return 0;
}

/*
 * Same split EDK2's gpi_elf_parser.c performs: separate the ELF header +
 * program headers + hash segment ("metadata", handed to TZ for
 * authentication) from the loadable program segment ("prog", the actual
 * firmware payload TZ maps and executes in place).
 */
struct pil_elf_split {
	void *meta_base;
	u32 meta_size;
	phys_addr_t prog_base;
	u32 prog_size;
};

static int pil_split_elf(void *elf_base, u32 elf_size, struct pil_elf_split *out)
{
	Elf32_Ehdr *ehdr = elf_base;
	Elf32_Phdr *phdrs;
	void *hash_base = NULL;
	u32 hash_size = 0;
	phys_addr_t prog_base = 0;
	u32 meta_size;
	u8 *meta;
	int i;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) ||
	    ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
	    !ehdr->e_phnum)
		return -EINVAL;

	phdrs = (Elf32_Phdr *)((u8 *)elf_base + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		Elf32_Phdr *phdr = &phdrs[i];
		u32 segtype = (phdr->p_flags & MI_PBT_FLAG_SEGMENT_TYPE_MASK) >>
			      MI_PBT_FLAG_SEGMENT_TYPE_SHIFT;

		if (segtype == MI_PBT_HASH_SEGMENT) {
			hash_base = (u8 *)elf_base + phdr->p_offset;
			hash_size = phdr->p_filesz;
		} else if (phdr->p_type == PT_LOAD && !prog_base) {
			prog_base = (phys_addr_t)((u8 *)elf_base + phdr->p_offset);
		}
	}

	if (!prog_base)
		return -EINVAL;

	meta_size = sizeof(*ehdr) + sizeof(Elf32_Phdr) * ehdr->e_phnum + hash_size;
	meta = memalign(QUPFW_ALIGN, ALIGN(meta_size, QUPFW_ALIGN));
	if (!meta)
		return -ENOMEM;

	memcpy(meta, ehdr, sizeof(*ehdr));
	memcpy(meta + sizeof(*ehdr), phdrs, sizeof(Elf32_Phdr) * ehdr->e_phnum);
	if (hash_size)
		memcpy(meta + sizeof(*ehdr) + sizeof(Elf32_Phdr) * ehdr->e_phnum,
		       hash_base, hash_size);

	out->meta_base = meta;
	out->meta_size = meta_size;
	out->prog_base = prog_base;
	/*
	 * Matches EDK2's gpi_fwload.c: "ProgSegSize = (ImgSize -
	 * (ProgSegBase - ElfBase))" -- from the first loadable segment's
	 * offset to the end of the raw ELF buffer, not the sum of each
	 * PT_LOAD segment's p_memsz, which TZ's own validation rejects.
	 */
	out->prog_size = elf_size - (u32)(prog_base - (phys_addr_t)elf_base);

	return 0;
}

/*
 * This board's eMMC is always UCLASS_MMC devnum 0. Go straight to it with
 * blk_get_devnum_by_uclass_id() instead of qcom_geni.c's
 * find_qupfw_part(), which walks every probed UCLASS_BLK device
 * (uclass_first_device()/uclass_next_device() probe on demand) -- that
 * bulk scan is the same one CONFIG_QCOM_GENI_FW_LATE_INIT's bulk path
 * hangs on for this board (see drivers/misc/Kconfig).
 */
#define QUPFW_MMC_DEVNUM	0

static void *qupfw_read_partition(u32 *sizep)
{
	struct blk_desc *desc;
	struct disk_partition part_info;
	void *fw_buf;
	u32 fw_size;
	int partnum;
	unsigned long nblks;

	desc = blk_get_devnum_by_uclass_id(UCLASS_MMC, QUPFW_MMC_DEVNUM);
	if (!desc)
		return NULL;

	for (partnum = 1; ; partnum++) {
		if (part_get_info(desc, partnum, &part_info))
			return NULL;
		if (!strcmp(part_info.type_guid, QUPFW_PART_TYPE_GUID))
			break;
	}

	fw_size = part_info.size * part_info.blksz;
	/* TZ XPU-locks this buffer at 4 KiB granularity -- base must be page aligned. */
	fw_buf = memalign(QUPFW_ALIGN, ALIGN(fw_size, QUPFW_ALIGN));
	if (!fw_buf)
		return NULL;

	nblks = blk_dread(desc, part_info.start, part_info.size, fw_buf);
	if (IS_ERR_VALUE(nblks) || nblks != part_info.size) {
		free(fw_buf);
		return NULL;
	}

	*sizep = fw_size;

	return fw_buf;
}

/**
 * qcom_scm_pas_unlock() - Unlock a TZ-protected GENI SE's firmware RAM.
 * @peripheral: PIL proc ID of the GENI SE wrapper to unlock
 *
 * Mirrors EDK2's GpiDrvLib gpi_load_firmware(): read the qupfw ELF, hand
 * its metadata and program segment to TZ via the PIL SCM sequence (init
 * -> mem_setup -> auth_and_reset -> unlock_xpu).
 *
 * Return: 0 on success, negative error code on failure.
 */
int qcom_scm_pas_unlock(u32 peripheral)
{
	void *elf_buf;
	u32 elf_size;
	struct pil_elf_split split;
	int ret;

	elf_buf = qupfw_read_partition(&elf_size);
	if (!elf_buf)
		return -ENOENT;

	ret = pil_split_elf(elf_buf, elf_size, &split);
	if (ret)
		goto free_elf;

	/*
	 * TZ reads physical memory directly for both the metadata (init) and
	 * the program segment (mem_setup/auth_and_reset) -- flush both out
	 * of the AP's cache first.
	 */
	flush_dcache_range((unsigned long)elf_buf,
			    (unsigned long)elf_buf + ALIGN(elf_size, QUPFW_ALIGN));
	flush_dcache_range((unsigned long)split.meta_base,
			    (unsigned long)split.meta_base + ALIGN(split.meta_size, QUPFW_ALIGN));

	ret = scm_pil_call(TZ_PIL_INIT_ID, scm_arginfo(2, 0, 2, 0),
			    peripheral, (unsigned long)split.meta_base, 0);
	if (ret)
		goto free_meta;

	ret = scm_pil_call(TZ_PIL_MEM_ID, scm_arginfo(3, 0, 0, 0),
			    peripheral, split.prog_base, split.prog_size);
	if (ret)
		goto free_meta;

	ret = scm_pil_call(TZ_PIL_AUTH_RESET_ID, scm_arginfo(1, 0, 0, 0),
			    peripheral, 0, 0);
	if (ret)
		goto free_meta;

	ret = scm_pil_call(TZ_PIL_UNLOCK_XPU_ID, scm_arginfo(1, 0, 0, 0),
			    peripheral, 0, 0);

free_meta:
	free(split.meta_base);
free_elf:
	free(elf_buf);

	return ret;
}
