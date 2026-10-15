// SPDX-License-Identifier: GPL-2.0+
/*
 * ufshcd-mcq.c - UFSHCI 4.0 Multi-Circular Queue (MCQ) support
 *
 * U-Boot's UFS driver only ever has one command outstanding at a time
 * (ufs-uclass.c's TASK_TAG is fixed at 0), so this port needs none of the
 * Linux driver's multi-queue / outstanding-request-registry machinery: a
 * single SQ/CQ pair (queue index 0), submit, then poll the same
 * completion queue back. No interrupts are used - this file only ever
 * polls the hardware tail/head pointers, matching the SDB path's own
 * polling style in ufshcd_send_command().
 */

#include <dm.h>
#include <log.h>
#include <malloc.h>
#include <memalign.h>
#include <phys2bus.h>
#include <time.h>
#include <dm/device_compat.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>

#include "ufs.h"
#include "ufshcd-mcq.h"

/* Submission/completion poll timeout - mirrors QUERY_REQ_TIMEOUT in
 * ufs-uclass.c, kept local since that constant isn't exported.
 */
#define MCQ_POLL_TIMEOUT_MS	1500

bool ufshcd_mcq_capable(struct ufs_hba *hba)
{
	return (hba->capabilities & MASK_MCQ_SUPPORT) &&
	       hba->ops && hba->ops->mcq_config_resource;
}

int ufshcd_mcq_alloc(struct ufs_hba *hba)
{
	hba->mcq_depth = UFSHCD_MCQ_DEPTH;

	hba->mcq_sqe_base = memalign(1024,
		ALIGN(sizeof(struct utp_transfer_req_desc) * hba->mcq_depth,
		      ARCH_DMA_MINALIGN));
	if (!hba->mcq_sqe_base) {
		dev_err(hba->dev, "MCQ SQ ring allocation failed\n");
		return -ENOMEM;
	}

	hba->mcq_cqe_base = memalign(1024,
		ALIGN(sizeof(struct cq_entry) * hba->mcq_depth,
		      ARCH_DMA_MINALIGN));
	if (!hba->mcq_cqe_base) {
		dev_err(hba->dev, "MCQ CQ ring allocation failed\n");
		return -ENOMEM;
	}

	memset(hba->mcq_sqe_base, 0,
	       sizeof(struct utp_transfer_req_desc) * hba->mcq_depth);
	memset(hba->mcq_cqe_base, 0,
	       sizeof(struct cq_entry) * hba->mcq_depth);
	ufshcd_cache_flush(hba->mcq_sqe_base,
			   sizeof(struct utp_transfer_req_desc) * hba->mcq_depth);
	ufshcd_cache_flush(hba->mcq_cqe_base,
			    sizeof(struct cq_entry) * hba->mcq_depth);

	return 0;
}

int ufshcd_mcq_make_operational(struct ufs_hba *hba)
{
	dma_addr_t sqe_dma_addr, cqe_dma_addr;
	u32 qsize;
	int err;

	err = ufshcd_vops_mcq_config_resource(hba);
	if (err) {
		dev_err(hba->dev, "MCQ resource config failed: %d\n", err);
		return err;
	}

	sqe_dma_addr = dev_phys_to_bus(hba->dev,
			(phys_addr_t)(uintptr_t)hba->mcq_sqe_base);
	cqe_dma_addr = dev_phys_to_bus(hba->dev,
			(phys_addr_t)(uintptr_t)hba->mcq_cqe_base);

	qsize = hba->mcq_depth * MCQ_ENTRY_SIZE_IN_DWORD - 1;

	/* Submission Queue base address + doorbell/interrupt-status offsets */
	writel(lower_32_bits(sqe_dma_addr), hba->mcq_cfg_base + REG_SQLBA);
	writel(upper_32_bits(sqe_dma_addr), hba->mcq_cfg_base + REG_SQUBA);
	writel((u32)((u8 *)hba->mcq_sqd_base - (u8 *)hba->mmio_base),
	       hba->mcq_cfg_base + REG_SQDAO);
	writel((u32)((u8 *)hba->mcq_sqis_base - (u8 *)hba->mmio_base),
	       hba->mcq_cfg_base + REG_SQISAO);

	/* Completion Queue base address + doorbell/interrupt-status offsets */
	writel(lower_32_bits(cqe_dma_addr), hba->mcq_cfg_base + REG_CQLBA);
	writel(upper_32_bits(cqe_dma_addr), hba->mcq_cfg_base + REG_CQUBA);
	writel((u32)((u8 *)hba->mcq_cqd_base - (u8 *)hba->mmio_base),
	       hba->mcq_cfg_base + REG_CQDAO);
	writel((u32)((u8 *)hba->mcq_cqis_base - (u8 *)hba->mmio_base),
	       hba->mcq_cfg_base + REG_CQISAO);

	hba->mcq_sq_tail_slot = 0;
	hba->mcq_cq_head_slot = 0;

	/* Enable Completion Queue, then Submission Queue (queue id 0) */
	writel(MCQ_QUEUE_ENABLE | qsize, hba->mcq_cfg_base + REG_CQATTR);
	writel(MCQ_QUEUE_ENABLE | qsize, hba->mcq_cfg_base + REG_SQATTR);

	/* Throttle max active commands to the depth we actually allocated */
	ufshcd_rmwl(hba, MCQ_CFG_MAC_MASK,
		    FIELD_PREP(MCQ_CFG_MAC_MASK, hba->mcq_depth - 1),
		    REG_UFS_MCQ_CFG);

	/* Switch the controller's data-command path over to MCQ */
	ufshcd_rmwl(hba, MCQ_MODE_SELECT, MCQ_MODE_SELECT, REG_UFS_MEM_CFG);

	dev_warn(hba->dev, "ufs: MCQ operational, depth=%d\n", hba->mcq_depth);

	return 0;
}

int ufshcd_mcq_send_command(struct ufs_hba *hba, unsigned int task_tag)
{
	struct utp_transfer_req_desc *sqe =
		&hba->mcq_sqe_base[hba->mcq_sq_tail_slot];
	unsigned long start;
	u32 cq_tail_slot;
	u32 sq_slot_used = hba->mcq_sq_tail_slot;
	static bool traced;

	memcpy(sqe, hba->utrdl, sizeof(*sqe));
	ufshcd_cache_flush(sqe, sizeof(*sqe));

	hba->mcq_sq_tail_slot++;
	if (hba->mcq_sq_tail_slot == hba->mcq_depth)
		hba->mcq_sq_tail_slot = 0;

	/* Make sure the SQE write lands before the controller is told to fetch it */
	wmb();
	writel(hba->mcq_sq_tail_slot * sizeof(struct utp_transfer_req_desc),
	       hba->mcq_sqd_base + REG_SQTP);

	start = get_timer(0);
	do {
		if (get_timer(start) > MCQ_POLL_TIMEOUT_MS) {
			dev_err(hba->dev, "MCQ: timed out waiting for completion\n");
			return -ETIMEDOUT;
		}
		cq_tail_slot = readl(hba->mcq_cqd_base + REG_CQTP) /
				sizeof(struct cq_entry);
	} while (cq_tail_slot == hba->mcq_cq_head_slot);

	ufshcd_cache_invalidate(&hba->mcq_cqe_base[hba->mcq_cq_head_slot],
				 sizeof(struct cq_entry));
	hba->mcq_last_ocs = hba->mcq_cqe_base[hba->mcq_cq_head_slot].overall_status;

	/*
	 * One-shot proof that live I/O actually transited the MCQ
	 * submit/poll/complete loop (not just that probe-time queue setup
	 * succeeded) - the SQ slot used, the CQ slot that completed, and a
	 * valid OCS pulled from the new cq_entry layout.
	 */
	if (!traced) {
		dev_warn(hba->dev,
			 "ufs: MCQ live I/O confirmed: sq_slot=%u -> cq_slot=%u, ocs=0x%x\n",
			 sq_slot_used, hba->mcq_cq_head_slot, hba->mcq_last_ocs);
		traced = true;
	}

	hba->mcq_cq_head_slot++;
	if (hba->mcq_cq_head_slot == hba->mcq_depth)
		hba->mcq_cq_head_slot = 0;
	writel(hba->mcq_cq_head_slot * sizeof(struct cq_entry),
	       hba->mcq_cqd_base + REG_CQHP);

	return 0;
}

int ufshcd_mcq_get_tr_ocs(struct ufs_hba *hba)
{
	return hba->mcq_last_ocs & MASK_OCS;
}
