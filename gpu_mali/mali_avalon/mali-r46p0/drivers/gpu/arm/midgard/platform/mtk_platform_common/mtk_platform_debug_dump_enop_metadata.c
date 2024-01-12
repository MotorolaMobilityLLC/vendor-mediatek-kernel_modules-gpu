// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <csf/mali_kbase_csf_firmware_log.h>

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include "mtk_platform_logbuffer.h"
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */

static bool global_debug_request_complete(struct kbase_device *const kbdev, u32 const req_mask)
{
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	bool complete = false;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	if ((kbase_csf_firmware_global_output(global_iface, GLB_DEBUG_ACK) & req_mask) ==
	    (kbase_csf_firmware_global_input_read(global_iface, GLB_DEBUG_REQ) & req_mask))
		complete = true;

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	return complete;
}

static bool global_request_complete(struct kbase_device *const kbdev,
				    u32 const req_mask)
{
	struct kbase_csf_global_iface *global_iface =
				&kbdev->csf.global_iface;
	bool complete = false;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	if ((kbase_csf_firmware_global_output(global_iface, GLB_ACK) & req_mask) ==
	    (kbase_csf_firmware_global_input_read(global_iface, GLB_REQ) & req_mask))
		complete = true;

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	return complete;
}

static int wait_for_global_request(struct kbase_device *const kbdev,
				   u32 const req_mask)
{
	const long wait_timeout =
		kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	long remaining;
	int err = 0;

	remaining = wait_event_timeout(kbdev->csf.event_wait,
				       global_request_complete(kbdev, req_mask),
				       wait_timeout);

	if (!remaining) {
		dev_warn(kbdev->dev, "Timed out waiting for global request %x to complete",
			 req_mask);
		err = -ETIMEDOUT;
	}

	return err;
}

static void set_global_debug_request(const struct kbase_csf_global_iface *const global_iface,
				     u32 const req_mask)
{
	u32 glb_debug_req;

	kbase_csf_scheduler_spin_lock_assert_held(global_iface->kbdev);

	glb_debug_req = kbase_csf_firmware_global_output(global_iface, GLB_DEBUG_ACK);
	glb_debug_req ^= req_mask;

	kbase_csf_firmware_global_input_mask(global_iface, GLB_DEBUG_REQ, glb_debug_req, req_mask);
}

static void set_global_request(
	const struct kbase_csf_global_iface *const global_iface,
	u32 const req_mask)
{
	u32 glb_req;

	kbase_csf_scheduler_spin_lock_assert_held(global_iface->kbdev);

	glb_req = kbase_csf_firmware_global_output(global_iface, GLB_ACK);
	glb_req ^= req_mask;
	kbase_csf_firmware_global_input_mask(global_iface, GLB_REQ, glb_req,
					     req_mask);
}

void mtk_debug_dump_enop_metatdata(struct kbase_device *kbdev)
{
	unsigned long flags;
	struct kbase_csf_global_iface *global_iface = &kbdev->csf.global_iface;
	/* Use the DDK native flow - GLB_DEBUG_RUN_MODE_TYPE_NOP for ENOP metadata dump */
	uint32_t run_mode = GLB_DEBUG_REQ_RUN_MODE_SET(0, GLB_DEBUG_RUN_MODE_TYPE_NOP);
	int ret = 0;

	mutex_lock(&kbdev->csf.reg_lock);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	/* Prepare GLB_DEBUG_REQ for ENOP metadata dump */
	set_global_debug_request(global_iface, GLB_DEBUG_REQ_DEBUG_RUN_MASK | run_mode);
	/* Prepare GLB_REQ for debug requeset */
	set_global_request(global_iface, GLB_REQ_DEBUG_CSF_REQ_MASK);
	/* Ring doorbell to CSFFW for debug request */
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	/* Wait finish of CSFFW process nop metadata dump */
	ret = wait_for_global_request(kbdev, GLB_REQ_DEBUG_CSF_REQ_MASK);
	if (!ret) {
		/* Check if CSFFW process nop metadata dump complete */
		if (global_debug_request_complete(kbdev, GLB_DEBUG_REQ_DEBUG_RUN_MASK))
			dev_info(kbdev->dev, "ENOP metadata dump complete!");
		else
			dev_info(kbdev->dev, "ENOP metadata dump failed!");
	}

	mutex_unlock(&kbdev->csf.reg_lock);

	/* Dump fw log with nop metadata */
	kbase_csf_firmware_log_dump_buffer(kbdev);
}