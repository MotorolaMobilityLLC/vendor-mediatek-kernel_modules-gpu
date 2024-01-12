// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __MTK_PLATFORM_COMMON_H__
#define __MTK_PLATFORM_COMMON_H__

#include <linux/platform_device.h>

enum mtk_common_debug_types {
	MTK_COMMON_DBG_DUMP_PM_STATUS,
	MTK_COMMON_DBG_DUMP_INFRA_STATUS,
	MTK_COMMON_DBG_CSF_DUMP_GROUPS_QUEUES,
	MTK_COMMON_DBG_TRIGGER_KERNEL_EXCEPTION,
	MTK_COMMON_DBG_TRIGGER_WARN_ON,
	MTK_COMMON_DBG_TRIGGER_BUG_ON,
};

struct kbase_device *mtk_common_get_kbdev(void);

bool mtk_common_pm_is_mfg_active(void);
void mtk_common_pm_mfg_active(void);
void mtk_common_pm_mfg_idle(void);

void mtk_common_debug(enum mtk_common_debug_types type, int pid);

int mtk_common_gpufreq_bringup(void);
int mtk_common_gpufreq_commit(int opp_idx);
int mtk_common_ged_dvfs_get_last_commit_idx(void);

int mtk_common_device_init(struct kbase_device *kbdev);
void mtk_common_device_term(struct kbase_device *kbdev);

#if IS_ENABLED(CONFIG_MALI_MTK_SYSFS)
void mtk_common_sysfs_init(struct kbase_device *kbdev);
void mtk_common_sysfs_term(struct kbase_device *kbdev);
#endif /* CONFIG_MALI_MTK_SYSFS */

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_FS)
void mtk_common_debugfs_init(struct kbase_device *kbdev);
void mtk_common_csf_debugfs_init(struct kbase_device *kbdev);
#endif /* CONFIG_MALI_MTK_DEBUG_FS */

int mtk_platform_pm_init(struct kbase_device *kbdev);
void mtk_platform_pm_term(struct kbase_device *kbdev);

#endif /* __MTK_PLATFORM_COMMON_H__ */

