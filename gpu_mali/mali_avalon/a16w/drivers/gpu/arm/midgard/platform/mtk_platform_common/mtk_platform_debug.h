// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
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

#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
/*
 * memory dump mode control for command stream buffers
 */
#define MTK_DEBUG_MEM_DUMP_DISABLE  0
#define MTK_DEBUG_MEM_DUMP_CS_BUFFER    0b001           /* dump command stream buffers */
#define MTK_DEBUG_MEM_DUMP_MASK     0b001
#endif /* CONFIG_MALI_MTK_DEBUG_DUMP */

struct mtk_debug_cs_queue_data {
    struct list_head queue_list;
    struct kbase_context *kctx;
    int group_type;     /* 0: active groups, 1: groups */
    u8 handle;
};

/* Dump infra status */
void mtk_debug_dump_infra_status_init(void);
void mtk_debug_dump_infra_status_term(void);
void mtk_debug_dump_infra_status(struct kbase_device *kbdev);

/* Dump gic status */
void mtk_debug_dump_gic_status(struct kbase_device *kbdev);

/* Dump pm status */
void mtk_debug_dump_pm_status(struct kbase_device *kbdev);

/* Dump ENOP metadata */
int mtk_debug_dump_enop_metadata_debugfs_init(struct kbase_device *kbdev);
int mtk_debug_dump_enop_metadata_init(struct kbase_device *kbdev);
void mtk_debug_dump_enop_metadata(struct kbase_device *kbdev);

#endif /* __MTK_PLATFORM_DEBUG_H__ */
