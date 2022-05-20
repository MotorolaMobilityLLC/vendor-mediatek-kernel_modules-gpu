// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __MTK_PLATFORM_LOGBUF_H__
#define __MTK_PLATFORM_LOGBUF_H__

#define MTK_LOG_BUFFER_NAME_LEN 64
#define MTK_LOG_BUFFER_ENTRY_SIZE 256

struct mtk_debug_logbuf {
	spinlock_t access_lock;
	uint32_t tail;
	uint32_t head;
	uint32_t size;
	char name[MTK_LOG_BUFFER_NAME_LEN];
	uint8_t tmp_entry[MTK_LOG_BUFFER_ENTRY_SIZE];
	uint8_t *entries;
	bool is_circular;
	bool fallback;
};

int mtk_log_buffer_init(struct kbase_device *kbdev);
int mtk_log_buffer_term(struct kbase_device *kbdev);
int mtk_log_buffer_procfs_init(struct kbase_device *kbdev, struct proc_dir_entry *parent);
int mtk_log_buffer_procfs_term(struct kbase_device *kbdev, struct proc_dir_entry *parent);
bool mtk_log_buffer_is_empty(struct mtk_debug_logbuf *logbuf);
bool mtk_log_buffer_is_full(struct mtk_debug_logbuf *logbuf);
void mtk_log_buffer_clear(struct mtk_debug_logbuf *logbuf);
void mtk_log_buffer_print(struct mtk_debug_logbuf *logbuf, const char *fmt, ...);
void mtk_log_buffer_dump(struct mtk_debug_logbuf *logbuf, struct seq_file *seq);

#endif /* __MTK_PLATFORM_LOGBUF_H__ */
