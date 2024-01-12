// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <mali_kbase.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <platform/mtk_platform_common.h>
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
#include "mtk_gpu_dvfs.h"
#endif
#include <mtk_gpufreq.h>
#include <ged_dvfs.h>
#if IS_ENABLED(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif

static bool mfg_powered;
static DEFINE_MUTEX(mfg_pm_lock);
static struct kbase_device *mali_kbdev;
#if IS_ENABLED(CONFIG_PROC_FS)
static struct proc_dir_entry *mtk_mali_root;
#endif

struct kbase_device *mtk_common_get_kbdev(void)
{
	return mali_kbdev;
}

bool mtk_common_pm_is_mfg_active(void)
{
	return mfg_powered;
}

void mtk_common_pm_mfg_active(void)
{
	mutex_lock(&mfg_pm_lock);
	mfg_powered = true;
	mutex_unlock(&mfg_pm_lock);
}

void mtk_common_pm_mfg_idle(void)
{
	mutex_lock(&mfg_pm_lock);
	mfg_powered = false;
	mutex_unlock(&mfg_pm_lock);
}

void mtk_common_debug_dump(void)
{
}

int mtk_common_gpufreq_bringup(void)
{
	static int bringup = -1;

	if (bringup == -1) {
#if defined(CONFIG_MTK_GPUFREQ_V2)
		bringup = gpufreq_bringup();
#else
		bringup = mt_gpufreq_bringup();
#endif
	}

	return bringup;
}

int mtk_common_gpufreq_commit(int opp_idx)
{
	int ret = -1;

	mutex_lock(&mfg_pm_lock);
	if (opp_idx >= 0 && mtk_common_pm_is_mfg_active()) {
#if defined(CONFIG_MTK_GPUFREQ_V2)
		ret = mtk_common_gpufreq_bringup() ?
			-1 : gpufreq_commit(TARGET_DEFAULT, opp_idx);
#else
		ret = mtk_common_gpufreq_bringup() ?
			-1 : mt_gpufreq_target(opp_idx, KIR_POLICY);
#endif /* CONFIG_MTK_GPUFREQ_V2 */
	}
	mutex_unlock(&mfg_pm_lock);

	return ret;
}

int mtk_common_ged_dvfs_get_last_commit_idx(void)
{
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	return (int)ged_dvfs_get_last_commit_idx();
#else
	return -1;
#endif
}


#if IS_ENABLED(CONFIG_PROC_FS)
static int mtk_common_gpu_utilization_show(struct seq_file *m, void *v)
{
#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	unsigned int util_active, util_3d, util_ta, util_compute, cur_opp_idx;

	mtk_common_update_gpu_utilization();

#if defined(CONFIG_MTK_GPUFREQ_V2)
	cur_opp_idx = mtk_common_gpufreq_bringup() ?
		0 : gpufreq_get_cur_oppidx(TARGET_DEFAULT);
#else
	cur_opp_idx = mtk_common_gpufreq_bringup() ?
		0 : mt_gpufreq_get_cur_freq_index();
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	util_active = mtk_common_get_util_active();
	util_3d = mtk_common_get_util_3d();
	util_ta = mtk_common_get_util_ta();
	util_compute = mtk_common_get_util_compute();

	seq_printf(m, "ACTIVE=%u 3D/TA/COMPUTE=%u/%u/%u OPP_IDX=%u MFG_PWR=%d\n",
	           util_active, util_3d, util_ta, util_compute, cur_opp_idx, mfg_powered);
#else
	seq_puts(m, "GPU DVFS doesn't be enabled\n");
#endif

	return 0;
}
DEFINE_PROC_SHOW_ATTRIBUTE(mtk_common_gpu_utilization);

void mtk_common_procfs_init(void)
{
  	mtk_mali_root = proc_mkdir("mtk_mali", NULL);
  	if (!mtk_mali_root) {
  		pr_info("cannot create /proc/%s\n", "mtk_mali");
  		return;
  	}
}

void mtk_common_procfs_exit(void)
{
	mtk_mali_root = NULL;
	remove_proc_entry("mtk_mali", NULL);
}
#endif /* CONFIG_PROC_FS */


int mtk_common_device_init(struct kbase_device *kbdev)
{
	if (!kbdev) {
		pr_info("@%s: kbdev is NULL\n", __func__);
		return -1;
	}

	mali_kbdev = kbdev;

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
#if IS_ENABLED(CONFIG_MALI_MTK_DVFS_LOADING_MODE)
	ged_dvfs_cal_gpu_utilization_ex_fp = mtk_common_cal_gpu_utilization_ex;
	mtk_notify_gpu_freq_change_fp = MTKGPUFreq_change_notify;
#else
	ged_dvfs_cal_gpu_utilization_fp = mtk_common_cal_gpu_utilization;
#endif /* CONFIG_MALI_MTK_DVFS_LOADING_MODE */
	ged_dvfs_gpu_freq_commit_fp = mtk_common_ged_dvfs_commit;
	ged_dvfs_set_gpu_core_mask_fp = mtk_set_core_mask;
#endif /* CONFIG_MALI_MIDGARD_DVFS && CONFIG_MALI_MTK_DVFS_POLICY */

	return 0;
}

void mtk_common_device_term(struct kbase_device *kbdev)
{
	if (!kbdev) {
		pr_info("@%s: kbdev is NULL\n", __func__);
		return;
	}

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
#if IS_ENABLED(CONFIG_MALI_MTK_DVFS_LOADING_MODE)
	ged_dvfs_cal_gpu_utilization_ex_fp = NULL;
	mtk_notify_gpu_freq_change_fp = NULL;
#else
	ged_dvfs_cal_gpu_utilization_fp = NULL;
#endif /* CONFIG_MALI_MTK_DVFS_LOADING_MODE */
	ged_dvfs_gpu_freq_commit_fp = NULL;
#endif /* CONFIG_MALI_MIDGARD_DVFS && CONFIG_MALI_MTK_DVFS_POLICY */
}
