// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <csf/mali_kbase_csf_firmware_log.h>
#include "backend/gpu/mali_kbase_pm_internal.h"
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include "mtk_platform_logbuffer.h"
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */

static int fw_analysis_dump_enable = 0;

#if IS_ENABLED(CONFIG_MALI_MTK_FW_ANALYSIS_DUMP)
static bool global_debug_request_complete(struct kbase_csf_fw_io *fw_io, u32 const req_mask)
{
	struct kbase_device *const kbdev = fw_io->kbdev;
	bool complete = false;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	if ((kbase_csf_fw_io_global_read(fw_io, GLB_DEBUG_ACK) & req_mask) ==
	    (kbase_csf_fw_io_global_input_read(fw_io, GLB_DEBUG_REQ) & req_mask))
		complete = true;

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	return complete;
}

static bool global_request_complete(struct kbase_csf_fw_io *fw_io,
				    u32 const req_mask)
{
	struct kbase_device *const kbdev = fw_io->kbdev;

	bool complete = false;
	unsigned long flags;

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	if ((kbase_csf_fw_io_global_read(fw_io, GLB_ACK) & req_mask) ==
	    (kbase_csf_fw_io_global_input_read(fw_io, GLB_REQ) & req_mask))
		complete = true;

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	return complete;
}


static int wait_for_global_request_with_timeout(struct kbase_csf_fw_io *fw_io, u32 const req_mask,
						unsigned int timeout_ms)
{
	struct kbase_device *const kbdev = fw_io->kbdev;
	const long wait_timeout = kbase_csf_timeout_in_jiffies(timeout_ms);
	long remaining;

	remaining = kbase_csf_fw_io_wait_event_timeout(fw_io, kbdev->csf.event_wait,
						       global_request_complete(fw_io, req_mask),
						       wait_timeout);

	if (!remaining) {
		dev_warn(kbdev->dev,
			 "[%llu] Timeout (%d ms) waiting for global request %x to complete when fw analysis dump, bypass dump",
			 kbase_backend_get_cycle_cnt(kbdev), timeout_ms, req_mask);
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			 "Timeout (%d ms) waiting for global request %x to complete when fw analysis dump, bypass dump\n",
			 timeout_ms, req_mask);
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */

		return -ETIMEDOUT;
	}

	return 0;
}

static int wait_for_global_request(struct kbase_csf_fw_io *fw_io, u32 const req_mask)
{
	return wait_for_global_request_with_timeout(
		fw_io, req_mask, kbase_get_timeout_ms(fw_io->kbdev, CSF_FIRMWARE_TIMEOUT));
}

static void set_global_debug_request(struct kbase_csf_fw_io *fw_io, u32 const req_mask)
{
	u32 glb_debug_req;

	kbase_csf_scheduler_spin_lock_assert_held(fw_io->kbdev);
	kbase_csf_fw_io_assert_opened(fw_io);

	glb_debug_req = kbase_csf_fw_io_global_read(fw_io, GLB_DEBUG_ACK);
	glb_debug_req ^= req_mask;

	kbase_csf_fw_io_global_write_mask(fw_io, GLB_DEBUG_REQ, glb_debug_req, req_mask);
}

static void set_global_request(struct kbase_csf_fw_io *fw_io, u32 const req_mask)
{
	u32 glb_req;

	kbase_csf_scheduler_spin_lock_assert_held(fw_io->kbdev);
	kbase_csf_fw_io_assert_opened(fw_io);

	glb_req = kbase_csf_fw_io_global_read(fw_io, GLB_ACK);
	glb_req ^= req_mask;
	kbase_csf_fw_io_global_write_mask(fw_io, GLB_REQ, glb_req, req_mask);
}

void mtk_debug_dump_fw_analysis(struct kbase_device *kbdev)
{
	unsigned long flags;
	struct kbase_csf_fw_io *fw_io = &kbdev->csf.fw_io;
	/* Use the DDK native flow - GLB_DEBUG_RUN_MODE_TYPE_NOP for fw analysis dump */
	uint32_t run_mode = GLB_DEBUG_REQ_RUN_MODE_SET(0, GLB_DEBUG_RUN_MODE_TYPE_NOP);
	int ret = 0;

	/* Check if fw analysis dump enabled */
	if (fw_analysis_dump_enable == 0)
		return;

	mutex_lock(&kbdev->csf.reg_lock);

	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	/* Prepare GLB_DEBUG_REQ for FW analysis dump */
	set_global_debug_request(fw_io, GLB_DEBUG_REQ_DEBUG_RUN_MASK | run_mode);

	/* Prepare GLB_REQ for debug requeset */
	set_global_request(fw_io, GLB_REQ_DEBUG_CSF_REQ_MASK);

	/* Ring doorbell to CSFFW for debug request */
	kbase_csf_ring_doorbell(kbdev, CSF_KERNEL_DOORBELL_NR);

	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	/* Wait finish of CSFFW process nop metadata dump */
	ret = wait_for_global_request(fw_io, GLB_REQ_DEBUG_CSF_REQ_MASK);
	if (!ret) {
		/* Check if CSFFW process nop metadata dump complete */
		if (global_debug_request_complete(fw_io, GLB_DEBUG_REQ_DEBUG_RUN_MASK))
			dev_info(kbdev->dev, "FW analysis dump complete!");
		else
			dev_info(kbdev->dev, "FW analysis dump failed!");
	}

	mutex_unlock(&kbdev->csf.reg_lock);

	/* Dump fw log with nop metadata */
	kbase_csf_firmware_log_dump_buffer(kbdev);
}

static void mtk_fw_analysis_dump_worker(struct work_struct *const data)
{
	struct kbase_device *kbdev = container_of(data, struct kbase_device, mtk_fw_analysis_dump_work);
	mtk_debug_dump_fw_analysis(kbdev);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int mtk_debug_fw_analysis_dump_enable_show(struct seq_file *m, void *v)
{
	seq_printf(m, "fw_analysis_dump_enable = %d\n", fw_analysis_dump_enable);

	return 0;
}

static int mtk_debug_fw_analysis_dump_enable_open(struct inode *in, struct file *file)
{
	struct kbase_device *kbdev = in->i_private;
	file->private_data = kbdev;

	if (file->f_mode & FMODE_WRITE)
		return 0;

	return single_open(file, mtk_debug_fw_analysis_dump_enable_show, in->i_private);
}

static int mtk_debug_fw_analysis_dump_enable_release(struct inode *in, struct file *file)
{
	if (!(file->f_mode & FMODE_WRITE)) {
		struct seq_file *m = (struct seq_file *)file->private_data;

		if (m)
			seq_release(in, file);
	}

	return 0;
}

static ssize_t mtk_debug_fw_analysis_dump_enable_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct kbase_device *kbdev = (struct kbase_device *)file->private_data;
	int ret = 0;
	int temp = 0;
	int original_setting = 0;
	CSTD_UNUSED(ppos);

	ret = kstrtoint_from_user(ubuf, count, 0, &temp);
	if (ret)
		return ret;

	original_setting = fw_analysis_dump_enable;

	if (temp == 1)
		fw_analysis_dump_enable = 1;
	else if (temp == 0)
		fw_analysis_dump_enable = 0;
	else if (temp == 5566) {
		/* For forcely direct dump testing */
		fw_analysis_dump_enable = 1;

		/* Power up the GPU */
		kbase_csf_scheduler_pm_active(kbdev);
		kbase_pm_wait_for_l2_powered(kbdev);

		/* Trigger FW analysis dump */
		mtk_debug_dump_fw_analysis(kbdev);

		/* Power down the GPU */
		kbase_csf_scheduler_pm_idle(kbdev);

		/* Restore the original dump setting */
		fw_analysis_dump_enable = original_setting;
	}

	dev_info(kbdev->dev, "@%s: fw_analysis_dump_enable = %d ", __func__, fw_analysis_dump_enable);

	return count;
}

static const struct file_operations mtk_debug_fw_analysis_dump_enable_fops = {
	.open    = mtk_debug_fw_analysis_dump_enable_open,
	.release = mtk_debug_fw_analysis_dump_enable_release,
	.read    = seq_read,
	.write   = mtk_debug_fw_analysis_dump_enable_write,
	.llseek  = seq_lseek
};

int mtk_debug_dump_fw_analysis_debugfs_init(struct kbase_device *kbdev)
{
	if (IS_ERR_OR_NULL(kbdev))
		return -1;

	debugfs_create_file("fw_analysis_dump_enable", 0444,
			kbdev->mali_debugfs_directory, kbdev,
			&mtk_debug_fw_analysis_dump_enable_fops);
	return 0;
}
#else /* CONFIG_DEBUG_FS */
int mtk_debug_dump_fw_analysis_debugfs_init(struct kbase_device *kbdev)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

int mtk_debug_dump_fw_analysis_init(struct kbase_device *kbdev)
{
	/* Default enable the fw analysis dump from boot up */
	fw_analysis_dump_enable = 1;

	kbdev->mtk_fw_analysis_dump_workq =
		alloc_workqueue("mtk_fw_analysis_dump_workq", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (kbdev->mtk_fw_analysis_dump_workq == NULL)
		dev_info(kbdev->dev, "@%s: mtk_fw_analysis_dump_workq init failed", __func__);

	INIT_WORK(&kbdev->mtk_fw_analysis_dump_work, mtk_fw_analysis_dump_worker);

	return 0;
}
#endif /* CONFIG_MALI_MTK_FW_ANALYSIS_DUMP */
