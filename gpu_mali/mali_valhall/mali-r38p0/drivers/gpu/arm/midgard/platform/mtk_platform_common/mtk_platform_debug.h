// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_PLATFORM_DEBUG_H__
#define __MTK_PLATFORM_DEBUG_H__

#define KBASE_PLATFORM_TAG  "[KBASE/PLATFORM]"
#define KBASE_PLATFORM_LOGD(fmt, args...) \
	do { if (KBASE_PLATFORM_DEBUG_ENABLE) \
            {pr_info(KBASE_PLATFORM_TAG"[DEBUG]@%s: "fmt"\n", __func__, ##args);} \
        else \
            {pr_debug(KBASE_PLATFORM_TAG"[DEBUG]@%s: "fmt"\n", __func__, ##args);} \
        } while (0)
#define KBASE_PLATFORM_LOGE(fmt, args...) \
	pr_info(KBASE_PLATFORM_TAG"[ERROR]@%s: "fmt"\n", __func__, ##args)
#define KBASE_PLATFORM_LOGI(fmt, args...) \
	pr_info(KBASE_PLATFORM_TAG"[INFO]@%s: "fmt"\n", __func__, ##args)

void mtk_common_debug_dump_pm_status(struct kbase_device *kbdev);
void mtk_debug_csf_dump_groups_and_queues(struct kbase_device *kbdev, int pid);
void mtk_debug_dump_for_external_fence(int fd, int pid, int type, int timeouts);

extern void (*mtk_gpu_fence_debug_dump_fp)(int fd, int pid, int type, int timeouts);

#endif /* __MTK_PLATFORM_DEBUG_H__ */
