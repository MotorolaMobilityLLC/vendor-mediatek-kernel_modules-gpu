/*
 * Copyright 2016 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Authors:
 * [ initial prototype implementation ]
 *   Dmitry Safonov <0x7f454c46@gmail.com> (ex-employee)
 * [ development of the current version ]
 *   Alexander Yashchenko <a.yashchenko@samsung.com>
 * [ small fixes ]
 *   Sergei Rogachev <s.rogachev@samsung.com>
 *
 * This file is part of MALI Midgard Reclaimer for NATIVE memory
 * allocations also known as "Midgard GMC" (graphical memory compression).
 *
 * "Midgard GMC" is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * "Midgard GMC" is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with "Midgard GMC".  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "kbase-gmc: " fmt

#include "mali_kbase_gmc.h"
#include "mali_kbase_debug.h"
#include "mali_kbase_mem_linux.h"
#include "mali_kbase_hwaccess_time.h"
#include <linux/delay.h>
#include <linux/pagemap.h>
#include <linux/bug.h>
#include <linux/freezer.h>
#include <linux/page-flags.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/seq_file.h>
#include <linux/migrate.h>
#include <linux/gfp.h>
#include "mali_kbase_mem.h"
#include "mmu/mali_kbase_mmu.h"
#include <tl/mali_kbase_tracepoints.h>
#include <mali_kbase_trace_gpu_mem.h>

static atomic_t n_gmc_workers = ATOMIC_INIT(0);
static atomic_t n_gmc_workers_failed = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(gmc_wait);

#define GMC_WORKER_TIMEOUT_MS 10000

#define GMC_MAX_COMPRESS_SIZE_IN_MEGA	1000
#define GMC_PAGES_PER_MEGA	((0x100000)>> PAGE_SHIFT)
#define GPU_MEMORY_SEQ_BUF_SIZE 8*PAGE_SIZE

/**
 * is_region_growable() - Check if a memory region is growable.
 * @reg: The memory region to check.
 *
 * Return: True if the region has the necessary flags to be growable,
 * false otherwise.
 */
static inline bool is_region_growable(struct kbase_va_region *reg)
{
	return GROWABLE_FLAGS_REQUIRED & reg->flags;
}

/**
 * seq_print_mem_type() - Helper to print memory type string for seq_file.
 * @sfile: The seq_file to print to.
 * @alloc: The memory allocation whose type will be printed.
 *
 * Prints a 4-character string representing the memory allocation type.
 */
static inline void seq_print_mem_type(struct seq_file *sfile,
		struct kbase_mem_phy_alloc *alloc)
{
	if (alloc)
		switch (alloc->type) {
			case KBASE_MEM_TYPE_NATIVE:
				seq_puts(sfile, "NATI");
				return;
			case KBASE_MEM_TYPE_IMPORTED_UMM:
				seq_puts(sfile, "IUMM");
				return;
			case KBASE_MEM_TYPE_ALIAS:
				seq_puts(sfile, "ALIA");
				return;
			case KBASE_MEM_TYPE_RAW:
				seq_puts(sfile, " RAW");
				return;
			case KBASE_MEM_TYPE_IMPORTED_USER_BUF:
				seq_puts(sfile, "USRB");
				return;
		}
	seq_puts(sfile, "UNKN");
}

/**
 * seq_print_gpu_mappings() - Helper to print GPU mapping count for seq_file.
 * @sfile: The seq_file to print to.
 * @alloc: The memory allocation whose GPU mapping count will be printed.
 */
static inline void seq_print_gpu_mappings(struct seq_file *sfile,
		struct kbase_mem_phy_alloc *alloc)
{
	seq_printf(sfile, "%6u",
			alloc ? atomic_read(&alloc->gpu_mappings) : 0);
}

/**
 * seq_print_flags() - Helper to print region access flags for seq_file.
 * @sfile: The seq_file to print to.
 * @reg:   The memory region whose flags will be printed.
 *
 * Prints CPU and GPU read/write/execute permissions.
 */
static inline void seq_print_flags(struct seq_file *sfile,
		struct kbase_va_region *reg)
{
	seq_printf(sfile, "%s%s%s %s%s%s",
		(KBASE_REG_CPU_CACHED & reg->flags) ?  "c" : "-",
		(KBASE_REG_CPU_RD & reg->flags) ?  "r" : "-",
		(KBASE_REG_CPU_WR & reg->flags) ?  "w" : "-",
		(KBASE_REG_GPU_RD & reg->flags) ?  "r" : "-",
		(KBASE_REG_GPU_WR & reg->flags) ?  "w" : "-",
		(KBASE_REG_GPU_NX & reg->flags) ?  "-" : "x");
}

/**
 * count_compressed_pages() - Count the number of compressed pages in an allocation.
 * @alloc: The memory allocation to inspect.
 *
 * Iterates through all page entries in a kbase_mem_phy_alloc structure
 * and counts how many are currently marked as compressed.
 *
 * Return: The total number of compressed pages.
 */
static inline size_t count_compressed_pages(struct kbase_mem_phy_alloc *alloc)
{
	size_t i, ret = 0;
	struct tagged_addr *p;

	for (i = 0; i < alloc->nents; i++){
		p = &alloc->pages[i];
		if (as_phys_addr_t(*p) && kbase_is_entry_compressed(*p))
			ret++;
	}
	return ret;
}

/**
 * seq_print_reg() - Helper to print a full summary of a memory region for seq_file.
 * @sfile: The seq_file to print to.
 * @reg:   The memory region to summarize.
 *
 * This function gathers and prints detailed information about a single
 * kbase_va_region, including its PFN, size, backing allocations,
 * compressed page count, and flags.
 */
static inline void seq_print_reg(struct seq_file *sfile,
		struct kbase_va_region *reg)
{
	struct kbase_mem_phy_alloc *cpu_alloc,*gpu_alloc;

	if(!reg)	return;

	cpu_alloc = reg->cpu_alloc;
	gpu_alloc = reg->gpu_alloc;
	if(!cpu_alloc || !gpu_alloc)
		return;

	kbase_mem_phy_alloc_get(cpu_alloc);
	kbase_mem_phy_alloc_get(gpu_alloc);
	KBASE_DEBUG_ASSERT(gpu_alloc->type == cpu_alloc->type);
	seq_printf(sfile, "    %8llu %7zu ",
			reg->start_pfn, reg->nr_pages);
	//no extent now
	seq_puts(sfile, "      -  ");

	seq_printf(sfile, "%8zu", cpu_alloc->nents);
	seq_puts(sfile, "       ");
	seq_printf(sfile, "%8zu", (gpu_alloc && (cpu_alloc != gpu_alloc)) ? gpu_alloc->nents : 0);
	seq_printf(sfile, " %7zu  ", count_compressed_pages(cpu_alloc));

	seq_print_flags(sfile, reg);
	seq_putc(sfile, ' ');
	seq_print_gpu_mappings(sfile, gpu_alloc);
	seq_puts(sfile, "   ");
	seq_print_mem_type(sfile, cpu_alloc);
	seq_putc(sfile, '\n');
	kbase_mem_phy_alloc_put(cpu_alloc);
	kbase_mem_phy_alloc_put(gpu_alloc);
}


/**
 * kbasep_show_kctx_overall_native_memory() - Summarize native memory for a context.
 * @sfile: The seq_file to print to.
 * @kctx:  The kbase_context to summarize.
 *
 * Iterates through all memory regions in a context, calculating the total
 * number of pages backed by native allocations and the total number of
 * those pages which are currently compressed. Prints a summary to the seq_file.
 */
static void kbasep_show_kctx_overall_native_memory(
		struct seq_file *sfile,
		struct kbase_context *kctx)
{
	struct rb_node *node;
	struct kbase_va_region *reg = NULL;
	unsigned long backed_pages_overall = 0;
	unsigned long compressed_pages_overall = 0;
	int i = 0;
	struct rb_root *rb_root;

	kbase_gpu_vm_lock(kctx);
	for(i = 0; i < 3 ; i ++){
		if(i == 0)
			rb_root = &(kbase_ctx_reg_zone_get(kctx, CUSTOM_VA_ZONE)->reg_rbtree) ;
		else if(i == 1)
			rb_root = &(kbase_ctx_reg_zone_get(kctx, EXEC_VA_ZONE)->reg_rbtree) ;
		else
			rb_root = &(kbase_ctx_reg_zone_get(kctx, SAME_VA_ZONE)->reg_rbtree) ;
		for_each_rb_node(rb_root, node) {
			reg = rb_entry(node, struct kbase_va_region, rblink);
			if (!is_region_free(reg) && reg->cpu_alloc &&
				reg->cpu_alloc->type == KBASE_MEM_TYPE_NATIVE) {
				backed_pages_overall += reg->cpu_alloc->nents;
				if (reg->cpu_alloc != reg->gpu_alloc) {
					KBASE_DEBUG_ASSERT(reg->cpu_alloc->nents != reg->gpu_alloc->nents);
					backed_pages_overall *= 2;
				}
				compressed_pages_overall += count_compressed_pages(reg->cpu_alloc);
				if (reg->cpu_alloc != reg->gpu_alloc) {
					compressed_pages_overall += count_compressed_pages(reg->gpu_alloc);
				}
			}
		}
	}
	kbase_gpu_vm_unlock(kctx);
	for (; i < 90; i++)
		seq_putc(sfile, '-');
	seq_putc(sfile, '\n');
	seq_printf(sfile, "native memory overall: %8lu (pages)\n", backed_pages_overall);
	seq_printf(sfile, "native memory compressed: %8lu (pages)\n", compressed_pages_overall);
	seq_putc(sfile, '\n');
}


/**
 * kbasep_gpu_memory_show_kctx() - Show detailed memory regions for a context.
 * @sfile: The seq_file to print to.
 * @kctx:  The kbase_context to show.
 *
 * Prints a detailed, multi-column header and then iterates through all
 * memory regions in the context, printing a summary line for each one
 * using seq_print_reg().
 */
static void kbasep_gpu_memory_show_kctx(
		struct seq_file *sfile,
		struct kbase_context *kctx)
{
	struct rb_node *node;
	struct kbase_va_region *reg = NULL;
	int i;
	struct rb_root *rb_root;

	kbase_gpu_vm_lock(kctx);
	seq_printf(sfile, /* header */
		"        pfn    pages  extent    backed(cpu) backed(gpu)  compr    flags  gpu_map type\n");

	/* through all gpu VAs */
	for(i = 0; i < 5 ; i ++){
		if(i == 0)
			rb_root = &(kbase_ctx_reg_zone_get(kctx, CUSTOM_VA_ZONE)->reg_rbtree) ;
		else if(i == 1)
			rb_root = &(kbase_ctx_reg_zone_get(kctx, EXEC_VA_ZONE)->reg_rbtree) ;
		else if(i == 2)
			rb_root = &(kbase_ctx_reg_zone_get(kctx, SAME_VA_ZONE)->reg_rbtree) ;
		else if(i == 3)
			rb_root = &(kbase_ctx_reg_zone_get(kctx, EXEC_FIXED_VA_ZONE)->reg_rbtree);
		else
			rb_root = &(kbase_ctx_reg_zone_get(kctx, FIXED_VA_ZONE)->reg_rbtree);

		for_each_rb_node(rb_root, node) {
			reg = rb_entry(node, struct kbase_va_region, rblink);
			if (!is_region_free(reg))
				seq_print_reg(sfile, reg);
		}
	}

	kbase_gpu_vm_unlock(kctx);
}


/**
 * gmc_memory_info_show() - The main callback function for the debugfs file.
 * @sfile: The seq_file to print to.
 * @data:  Unused.
 *
 * This function is registered with the kernel's seq_file interface to generate
 * the content of the debugfs file. It iterates through all Mali devices, and for
 * each device, it iterates through all active contexts (processes), printing
 * detailed memory usage information for each.
 *
 * Return: 0 on success.
 */
int gmc_memory_info_show(struct seq_file *sfile, void *data)
{
	struct list_head *entry;
	const struct list_head *kbdev_list;

	kbdev_list = kbase_device_get_list();
	list_for_each(entry, kbdev_list) {
		struct kbase_device *kbdev = NULL;
                struct kbase_context *kctx;
		//struct kbasep_kctx_list_element *element;

		kbdev = list_entry(entry, struct kbase_device, entry);
		/* output the total memory usage and cap for this device */
		seq_printf(sfile, "%-16s  %10u\n",
				kbdev->devname,
				atomic_read(&(kbdev->memdev.used_pages)) -
				atomic_read(&(kbdev->memdev.compressed_pages)));

		mutex_lock(&kbdev->kctx_list_lock);
		list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
			/* output the memory usage and cap for each kctx
			* opened on this device */

			struct task_struct *tsk;
			char tsk_name[TASK_COMM_LEN] = "";
			//struct kbase_context *kctx = element->kctx;

			rcu_read_lock();
			tsk = find_task_by_vpid(kctx->tgid);
			if (tsk) {
				get_task_comm(tsk_name, tsk);
			}
			rcu_read_unlock();

			seq_printf(sfile,
				"  kctx-0x%pK pid:%u tgid:%u comm:%s all: %u OOM see:%u\n",
				kctx, kctx->pid, kctx->tgid, tsk_name,
				atomic_read(&kctx->used_pages) -
				atomic_read(&kctx->compressed_pages),
				atomic_read(&kctx->nonmapped_pages));
			kbasep_gpu_memory_show_kctx(sfile, kctx);
			kbasep_show_kctx_overall_native_memory(sfile, kctx);
		}
		mutex_unlock(&kbdev->kctx_list_lock);
	}

	kbase_device_put_list(kbdev_list);
	return 0;
}


/**
 * kbase_gmc_unlock_task - Release locks acquired for a GMC operation.
 * @gmc_tsk: Structure containing context and task information.
 * @op:      The GMC operation that was performed (compress or decompress).
 *
 * This function is the counterpart to kbase_gmc_trylock_task. It releases
 * the locks in the reverse order they were acquired. For compression, this
 * includes releasing the mmap_sem and dropping task/mm references. For all
 * operations, it releases the GPU VM lock.
 */
static void kbase_gmc_unlock_task(struct kbase_gmc_tsk *gmc_tsk, gmc_op op)
{
	if (!(op == GMC_COMPRESS)) {
		kbase_gpu_vm_unlock(gmc_tsk->kctx);
	} else {
		kbase_gpu_vm_unlock(gmc_tsk->kctx);
		mmap_write_unlock(gmc_tsk->mm);
		mmdrop(gmc_tsk->mm);
		put_task_struct(gmc_tsk->task);
	}
}

/**
 * kbase_gmc_trylock_task - Acquire necessary locks for a GMC operation.
 * @gmc_tsk: Structure containing context and task information.
 * @op:      The GMC operation to be performed (compress or decompress).
 *
 * This function acquires all locks needed to safely (de)compress memory
 * associated with a process.
 *
 * For decompression, it only needs to lock the GPU's virtual memory space.
 *
 * For compression, the operation is more complex because it involves removing
 * CPU mappings. It must find the target task, take references to the task
 * and its memory space (mm_struct), and acquire both the process's mmap_sem
 * for writing and the GPU's VM lock.
 *
 * Return: 0 on success, or a negative error code if the task or its
 * memory space has already been destroyed.
 */
static int kbase_gmc_trylock_task(struct kbase_gmc_tsk *gmc_tsk, gmc_op op)
{
	struct task_struct *tsk;
	if (!(op == GMC_COMPRESS)) {
		/*op != GMC_COMPRESS */
		kbase_gpu_vm_lock(gmc_tsk->kctx);
	} else {
		/*op == GMC_COMPRESS */
		lockdep_assert_held(&gmc_tsk->kctx->reg_lock);
		rcu_read_lock();
		tsk = find_task_by_vpid(gmc_tsk->kctx->tgid);
		if (!tsk) {
			/* Task is gone nothing to do */
			rcu_read_unlock();
			pr_warn("task is gone, can't lock\n");
			return -ESRCH;
		}
		get_task_struct(tsk);
		rcu_read_unlock();
		task_lock(tsk);
		if (tsk->mm)
			atomic_inc(&tsk->mm->mm_count);
		else {
			task_unlock(tsk);
			put_task_struct(tsk);
			pr_warn("mm is gone, can't lock\n");
			return -ESRCH;
		}
		gmc_tsk->mm = tsk->mm;
		task_unlock(tsk);
		mmap_write_lock(gmc_tsk->mm);
		kbase_gpu_vm_lock(gmc_tsk->kctx);
		gmc_tsk->task = tsk;
	}
	return 0;
}
/**
 * kbase_gmc_dma_unmap_page() - Unmap a page from the DMA controller.
 * @kbdev: The kbase device.
 * @page:  The page to unmap.
 *
 * This is a wrapper around the kernel's dma_unmap_page function. It also
 * clears the 'Private' flag on the page, which is used to track DMA-mapped
 * pages.
 */
void kbase_gmc_dma_unmap_page(struct kbase_device *kbdev, struct page *page)
{
	dma_unmap_page(kbdev->dev, kbase_dma_addr(page),
			PAGE_SIZE, DMA_BIDIRECTIONAL);
	lock_page(page);
	set_page_private(page,0);
	ClearPagePrivate(page);
	unlock_page(page);
}

/**
 * kbase_gmc_dma_map_page() - Map a page for use by the DMA controller.
 * @kbdev: The kbase device.
 * @page:  The page to map.
 *
 * This is a wrapper around the kernel's dma_map_page function. It maps the
 * page for DMA access from the device. On success, it sets the 'Private'
 * flag on the page and stores the resulting DMA address for later use.
 *
 * Return: The DMA address on success, or 0 on a mapping error.
 */
dma_addr_t kbase_gmc_dma_map_page(struct kbase_device *kbdev, struct page *page)
{
	dma_addr_t dma_addr;
	dma_addr = dma_map_page(kbdev->dev, page, 0, PAGE_SIZE,
			DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kbdev->dev, dma_addr)) {
		pr_alert("%s: dma_mapping_error!\n", __func__);
		return (dma_addr_t)(0ULL);
	}
	lock_page(page);
	SetPagePrivate(page);
	kbase_set_dma_addr_as_priv(page, dma_addr);
	unlock_page(page);
	return dma_addr;
}

/**
 * kbase_gmc_page_decompress() - Decompress a single physical page.
 * @p:     Pointer to the tagged address entry for the page.
 * @kbdev: The kbase device.
 *
 * This function orchestrates the decompression of a single page. It allocates
 * a new physical page, retrieves the compressed data from the GMC storage
- * backend into the new page, maps the new page for DMA, and updates the
 * tagged address pointer to point to the new physical page, clearing the
 * compressed flag.
 *
 * Return: 0 on success, or a negative error code on failure (e.g., -ENOMEM).
 */
static int kbase_gmc_page_decompress(struct tagged_addr *p,  struct kbase_mem_phy_alloc *alloc)
{
	struct page *page;
	dma_addr_t dma_addr;
	int err;
	gfp_t flags;
	struct gmc_storage_handle *handle;
	struct kbase_device *kbdev = alloc->imported.native.kctx->kbdev;

	BUG_ON(!kbase_is_entry_compressed(*p));
	handle = kbase_get_gmc_handle(p);
#if defined(CONFIG_ARM) && !defined(CONFIG_HAVE_DMA_ATTRS) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
	flags = GFP_USER | __GFP_ZERO;
#else
	flags = GFP_HIGHUSER | __GFP_ZERO;
#endif
	page = alloc_page(flags);
	if (!page) {
		pr_warn("Unable to allocate a page from buddy system.\n");
		return -ENOMEM;
	}
	err = gmc_storage_get_page(kbdev->kbase_gmc_device.storage, page, handle);
	if (err) {
		pr_alert("%s: can't get page for handle: %pK err: %d\n", __func__,
				handle, (int)err);
		__free_page(page);
		return -EINVAL;
	}
	dma_addr = kbase_gmc_dma_map_page(kbdev, page);
	dma_sync_single_for_device(kbdev->dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	p->tagged_addr = page_to_phys(page);
	kbase_clear_entry_compressed(p);
	p->tagged_addr |=  (p->gmc_flag & ~PAGE_MASK);
	p->gmc_flag = 0;
	BUG_ON(dma_addr != as_phys_addr_t(*p));
	atomic_dec( &alloc->imported.native.kctx->kbdev->memdev.compressed_pages);
	atomic_dec( &alloc->imported.native.kctx->compressed_pages);
	return 0;
}

/**
 * kbase_gmc_page_compress() - Compress a single physical page.
 * @p:     Pointer to the tagged address entry for the page.
 * @kbdev: The kbase device.
 *
 * This function orchestrates the compression of a single page. It unmaps the
 * page from the DMA controller, passes it to the GMC storage backend to be
 * compressed and stored, and on success, updates the tagged address entry
 * to store the GMC handle and marks the entry as compressed. Finally, it calls
 * put_page() to release the original physical page back to the kernel.
 *
 * Return: 0 on success. Returns -EINVAL if the page can't be compressed or
 * if there was an issue with the storage backend.
 */
static int kbase_gmc_page_compress(struct tagged_addr *p, struct kbase_mem_phy_alloc *alloc)
{
	struct page *page;
	struct gmc_storage_handle *handle;
	struct kbase_page_metadata * page_md;

	struct kbase_device *kbdev = alloc->imported.native.kctx->kbdev;

	BUG_ON(kbase_is_entry_compressed(*p));
	page = pfn_to_page(PFN_DOWN(as_phys_addr_t(*p)));
	if (!kbase_dma_addr(page)) {
		pr_warn("compression for physaddr %llu initiated,\
			but dma address was not set\n", (unsigned long long) as_phys_addr_t(*p));
		return -EINVAL;
	}
	dma_sync_single_for_cpu(kbdev->dev, kbase_dma_addr(page), PAGE_SIZE, DMA_BIDIRECTIONAL);
	kbase_gmc_dma_unmap_page(kbdev, page);
	handle = gmc_storage_put_page(kbdev->kbase_gmc_device.storage, page);
	if (IS_ERR(handle)) {
		dma_addr_t dma_addr;
		dma_addr = kbase_gmc_dma_map_page(kbdev, page);
		BUG_ON(dma_addr != as_phys_addr_t(*p) );
		if (PTR_ERR(handle) == -ENOMEM) {
			pr_alert("can't compress physaddr %llu, err: %pK\n",
				(unsigned long long) as_phys_addr_t(*p), handle);
			return -EINVAL;
		}
		pr_warn("physaddr %llu is badly compressed, skipping it\n", (unsigned long long) as_phys_addr_t(*p));
		p->tagged_addr |=p->gmc_flag;
		p->gmc_flag=0;
		return 0;
	}

	kbase_set_gmc_handle(handle,p);
	__free_page(page);
	atomic_inc( &alloc->imported.native.kctx->kbdev->memdev.compressed_pages);
	atomic_inc( &alloc->imported.native.kctx->compressed_pages);
	return 0;
}

/**
 * region_should_be_compressed() - Check if a memory region is eligible for compression.
 * @reg: The memory region to check.
 *
 * This function contains the policy for deciding if a region can be compressed.
 * For example, it excludes regions that are marked for deletion soon
 * (KBASE_REG_DONT_NEED) or regions that use special features like infinite cache.
 *
 * Return: True if the region can be compressed, false otherwise.
 */
static bool region_should_be_compressed(struct kbase_va_region *reg)
{
	bool compress = true;
	struct kbase_context *kctx = kbase_reg_to_kctx(reg);
	if (reg->cpu_alloc != reg->gpu_alloc) {
		/*Now don't touch regions with infinite cache feature */
		compress = false;
	} else if (reg->flags & KBASE_REG_DONT_NEED) {
		/*This region will be deleted soon*/
		compress = false;
	}else if (!kctx){
            compress = false;
        }
#if MALI_USE_CSF
       else if(reg->flags & KBASE_REG_CSF_EVENT)
               compress=false;
#endif

	return compress;
}


/**
 * kbase_gmc_compress_alloc() - Compress a range of pages in a memory allocation.
 * @alloc:     The memory allocation containing the pages.
 * @start_idx: The starting index within the allocation's page array.
 * @nr:        The number of pages to compress.
 *
 * Iterates through a specified range of pages in a kbase_mem_phy_alloc
 * structure and calls kbase_gmc_page_compress() for each valid, decompressed page.
 *
 * Return: 0 on success, or a negative error code if any page fails to compress.
 */
static int kbase_gmc_compress_alloc(struct kbase_mem_phy_alloc *alloc, u64 start_idx, size_t nr)
{
	int ret = 0;
	unsigned int i;
	struct tagged_addr *tagged_page = NULL;

	for (i = start_idx; i < start_idx + nr; i++) {
		tagged_page = &alloc->pages[i];
		if(is_huge(alloc->pages[i]))		//don't compress huge head
		{
			pr_warn("is huge page, skip\n");
			continue;
		}

		if (as_phys_addr_t(*tagged_page) && kbase_is_entry_decompressed(*tagged_page)) {
			tagged_page->gmc_flag = tagged_page->tagged_addr & ~PAGE_MASK;
			tagged_page->tagged_addr = as_phys_addr_t(*tagged_page);
			ret = kbase_gmc_page_compress(tagged_page, alloc);
			if (ret) {
				pr_warn("can't compress page, physaddr %llu\n", (unsigned long long) as_phys_addr_t(*tagged_page));
				tagged_page->tagged_addr |= tagged_page->gmc_flag;
				tagged_page->gmc_flag = 0;
				return ret;
			}
		}
	}
	return ret;
}

/**
 * kbase_gmc_compress_region() - Prepare and compress all pages in a memory region.
 * @reg:  The memory region to compress.
 * @vpfn: The starting virtual page frame number (unused in this implementation).
 * @nr:   The number of pages to compress (unused in this implementation).
 *
 * This function first checks if the region is eligible for compression.
 * It then unmaps the region's pages from both the CPU and GPU to ensure
 * they are not in use. Finally, it calls kbase_gmc_compress_alloc() to
 * perform the actual compression.
 *
 * Note: This function must be called while holding the context's mmap_sem
 * and gpu_vm_lock.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int kbase_gmc_compress_region(struct kbase_va_region *reg, u64 vpfn, size_t nr)
{
	int ret = 0;
	struct kbase_context *kctx = kbase_reg_to_kctx(reg);
	if (!kctx || !region_should_be_compressed(reg)) {
		pr_warn("kctx is null \n");
		return ret;
	}

	/* unmap all pages from CPU */
	kbase_mem_shrink_cpu_mapping(kctx, reg, 0, reg->cpu_alloc->nents);

	/* unmap all pages from GPU */
	ret = kbase_mem_shrink_gpu_mapping(kctx, reg, 0, reg->gpu_alloc->nents);
	if (ret){
		pr_info("shrink gpu mapping failed\n");
		return ret;
	}
	return kbase_gmc_compress_alloc(reg->cpu_alloc, vpfn - reg->start_pfn, nr);
}

/**
 * kbase_gmc_invalidate_alloc() - Invalidate compressed pages in an allocation.
 * @kctx:      The kbase context.
 * @start:     Pointer to the first tagged address entry to invalidate.
 * @pages_num: The number of entries to invalidate.
 *
 * This function is called when memory is being freed. It iterates through
 * the page entries and, for any that are compressed, it informs the GMC
 * storage backend that the data is no longer needed. This allows the backend
 * to reclaim its own storage space. The page table entry is then cleared.
 */
void kbase_gmc_invalidate_alloc(struct kbase_context *kctx,
		struct tagged_addr *start, size_t pages_num)
{
	size_t i;

	lockdep_assert_held(&kctx->reg_lock);

	for (i = 0; i < pages_num; i++) {
		struct tagged_addr *p = &start[i];

		if (as_phys_addr_t(*p) && kbase_is_entry_compressed(*p)) {
			pr_debug("%s handle %pK\n", __func__, kbase_get_gmc_handle(p));
			gmc_storage_invalidate_page(kctx->kbdev->kbase_gmc_device.storage,
				kbase_get_gmc_handle(p));
			p->tagged_addr = 0;
			kbase_clear_entry_compressed(p);
			p->tagged_addr |= p->gmc_flag;
			atomic_dec( &kctx->kbdev->memdev.compressed_pages);
			atomic_dec( &kctx->compressed_pages);
		}
	}
}

/**
 * kbase_gmc_decompress_alloc() - Decompress a range of pages in a memory allocation.
 * @alloc:     The memory allocation containing the pages.
 * @start_idx: The starting index within the allocation's page array.
 * @nr:        The number of pages to decompress.
 *
 * Iterates through a specified range of pages in a kbase_mem_phy_alloc
 * structure and calls kbase_gmc_page_decompress() for each page that is
 * currently marked as compressed.
 *
 * Return: 0 on success, or a negative error code if any page fails to decompress.
 */
static int kbase_gmc_decompress_alloc(struct kbase_mem_phy_alloc *alloc, s64 start_idx, size_t nr)
{
	int i,nr_size,ret = 0;
	struct tagged_addr *tagged_page = NULL;
	nr_size = (int)nr;

	for (i = start_idx; i < start_idx + nr_size; i++) {
		if(i < 0)		//patch for pgoff 3times aligment case
			continue;
		tagged_page = &alloc->pages[i];
		if(is_huge(alloc->pages[i]))		//bypass huge head because we didn't  compress it
			continue;
		if (as_phys_addr_t(*tagged_page) && kbase_is_entry_compressed(*tagged_page)) {
			ret = kbase_gmc_page_decompress(tagged_page, alloc);
			if (ret) {
				pr_warn("can't decompress page, physaddr %llu\n", (unsigned long long) as_phys_addr_t(*tagged_page));
				return ret;
			}
		}
	}
	return ret;
}

/**
 * kbase_gmc_decompress_region() - Decompress pages in a region and remap them.
 * @reg:  The memory region to decompress.
 * @vpfn: The starting virtual page frame number of the range to decompress.
 * @nr:   The number of pages to decompress.
 *
 * This function first calls kbase_gmc_decompress_alloc() to perform the
 * actual decompression. After the pages are decompressed (i.e., they have
 * valid physical pages again), it calls kbase_mmu_insert_pages() to remap
 * them into the GPU's page tables so the GPU can access them again.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int kbase_gmc_decompress_region(struct kbase_va_region *reg, u64 vpfn, size_t nr)
{
	int ret = 0;
	struct kbase_context *kctx = kbase_reg_to_kctx(reg);
    if (!kctx) return ret;
        const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_SYNC;
	ret = kbase_gmc_decompress_alloc(reg->cpu_alloc, (s64)(vpfn - reg->start_pfn), nr);
	//if decompress failed do not insert page entry into mmu
	if(ret)
		return ret;
        //		ret_insert = kbase_mmu_insert_pages(reg->kctx,vpfn,
//            					&reg->cpu_alloc->pages[vpfn - reg->start_pfn], nr, reg->flags);
	if(kctx->set_pt_flag)
        ret = kbase_mmu_insert_pages(kctx->kbdev, &kctx->mmu, vpfn,&reg->cpu_alloc->pages[vpfn - reg->start_pfn],
                                           nr,  reg->flags, kctx->as_nr,
                                           reg->cpu_alloc->group_id, mmu_sync_info, reg);
              
	return ret;
}


/**
 * kbase_gmc_walk_region() - Perform a GMC operation on a single memory region.
 * @reg: The memory region to process.
 * @op:  The operation to perform (GMC_COMPRESS or GMC_DECOMPRESS).
 *
 * This function acts as a dispatcher based on the requested operation. It
 * filters out non-native memory types and then calls either
 * kbase_gmc_compress_region() or kbase_gmc_decompress_region().
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int kbase_gmc_walk_region(struct kbase_va_region *reg, gmc_op op)
{
	int ret = 0;
	struct kbase_mem_phy_alloc *cpu_alloc = reg->cpu_alloc;
	struct kbase_mem_phy_alloc *gpu_alloc = reg->gpu_alloc;

	if (!cpu_alloc){
		pr_warn("gmc cpu_alloc zerro\n");
		return ret;
	}

	kbase_mem_phy_alloc_get(cpu_alloc);
	kbase_mem_phy_alloc_get(gpu_alloc);

	/* if allocation is KBASE_MEM_TYPE_IMPORTED_UMM i.e.,
	 * it's used for DMA operations between drivers, so
	 * don't touch it (it's usally DRM/GEM memory) */
	if (cpu_alloc->type != KBASE_MEM_TYPE_NATIVE) {
		kbase_mem_phy_alloc_put(gpu_alloc);
		kbase_mem_phy_alloc_put(cpu_alloc);
		pr_warn("gmc cpu_alloc not native memory\n");
		return ret;
	}

	switch (op) {
	case GMC_DECOMPRESS:
		ret = kbase_gmc_decompress_region(reg, reg->start_pfn, cpu_alloc->nents);
		break;
	case GMC_COMPRESS:
		ret = kbase_gmc_compress_region(reg, reg->start_pfn, cpu_alloc->nents);
		break;
	default:
		pr_warn("Invalid GMC operation\n");
		ret = -EINVAL;
	}

	kbase_mem_phy_alloc_put(gpu_alloc);
	kbase_mem_phy_alloc_put(cpu_alloc);
	return ret;
}

/**
 * kbase_gmc_walk_region_work() - The kernel work_struct function for GMC operations.
 * @work: Pointer to the work_struct, embedded in a kbase_va_region.
 *
 * This function is the entry point for the asynchronous worker threads that
 * perform the (de)compression. It retrieves the region from the work struct
 * and calls kbase_gmc_walk_region() to do the actual work. It also handles
 * worker counting and wakeup notifications.
 */
void kbase_gmc_walk_region_work(struct work_struct *work)
{
	int ret = 0;
	struct kbase_va_region *reg = container_of(work, struct kbase_va_region, gmc_work);
	ret = kbase_gmc_walk_region(reg, reg->op);
	if (ret) {
		pr_warn("worker [%pK] has errors during operation %d\n", work, reg->op);
		atomic_inc(&n_gmc_workers_failed);
	}
	atomic_dec(&n_gmc_workers);
	wake_up_interruptible(&gmc_wait);
}

/**
 * kbase_gmc_walk_each_reg() - Iterate and queue work for regions in a tree.
 * @rb_region: The red-black tree of regions to walk.
 * @arg:       The GMC arguments (operation and size).
 * @nr_pages_to_compressed: In/out pointer to the remaining number of pages to compress.
 *
 * Iterates through all regions in a given red-black tree and queues a
 * work item on the system's unbound workqueue for each one to be processed
 * asynchronously. For compression, it stops once the requested number of
 * pages has been queued.
 *
 * Return: Always 0.
 */
static int kbase_gmc_walk_each_reg(struct rb_root *rb_region, struct kbase_gmc_arg arg,int *nr_pages_to_compressed)
{
	struct rb_node *node;
	struct kbase_va_region *reg = NULL;

	if(*nr_pages_to_compressed <= 0)
		return 0;

	for_each_rb_node(rb_region, node) {
		reg = rb_entry(node, struct kbase_va_region, rblink);
		if (!is_region_free(reg)) {
			reg->op = arg.op;

			atomic_inc(&n_gmc_workers);
			queue_work(system_unbound_wq, &reg->gmc_work);

			if(arg.op == GMC_COMPRESS){
				*nr_pages_to_compressed -= reg->cpu_alloc->nents;
				if(*nr_pages_to_compressed <= 0)
					break;	//only compress arg->compress_size
			}
		}
	}

	return 0;
}

/**
 * kbase_gmc_walk_kctx() - Perform a GMC operation on all regions in a context.
 * @kctx: The kbase_context to process.
 * @arg:  The GMC arguments (operation and size).
 *
 * This is a high-level function that orchestrates a GMC operation for an
 * entire process context. It acquires the necessary locks, iterates through
 * all three region trees for the context (queuing work for each region),
 * and then waits for all worker threads to complete before releasing the locks.
 *
 * Return: 0 on success, -ETIMEDOUT if workers take too long, or -EINVAL
 * if any worker reported a failure.
 */
static int kbase_gmc_walk_kctx(struct kbase_context *kctx, struct kbase_gmc_arg arg)
{
	int ret = 0;
	struct rb_node *node;
	struct kbase_va_region *reg = NULL;
	struct kbase_gmc_tsk gmc_tsk;
	int n_workers_failed = 0;
	int nr_pages_to_compressed = GMC_MAX_COMPRESS_SIZE_IN_MEGA * GMC_PAGES_PER_MEGA;
	long timeout_jiff = msecs_to_jiffies(GMC_WORKER_TIMEOUT_MS);

	gmc_tsk.kctx = kctx;
	gmc_tsk.task = NULL;
	if(arg.op == GMC_COMPRESS){
		if(arg.compress_size >= GMC_MAX_COMPRESS_SIZE_IN_MEGA || arg.compress_size == 0)
			arg.compress_size = GMC_MAX_COMPRESS_SIZE_IN_MEGA;
		nr_pages_to_compressed = arg.compress_size * GMC_PAGES_PER_MEGA;	//in  pages
	}

	KBASE_DEBUG_ASSERT(!atomic_read(&n_gmc_workers));
	atomic_set(&n_gmc_workers_failed, 0);
	ret = kbase_gmc_trylock_task(&gmc_tsk, arg.op);
	if (ret) {
		pr_info("gmc trylock task failed\n");
		return ret;
	}

	if(arg.op == GMC_DECOMPRESS)
		kctx->set_pt_flag = true;

	kbase_gmc_walk_each_reg(&(kbase_ctx_reg_zone_get(kctx, SAME_VA_ZONE)->reg_rbtree) ,arg,&nr_pages_to_compressed);
	kbase_gmc_walk_each_reg(&(kbase_ctx_reg_zone_get(kctx, CUSTOM_VA_ZONE)->reg_rbtree) ,arg,&nr_pages_to_compressed);
	kbase_gmc_walk_each_reg(&(kbase_ctx_reg_zone_get(kctx, EXEC_VA_ZONE)->reg_rbtree),arg,&nr_pages_to_compressed);

	while (atomic_read(&n_gmc_workers) > 0) {
		int err = wait_event_interruptible_timeout(gmc_wait, !atomic_read(&n_gmc_workers),timeout_jiff);
		if (err <= 0) {
			pr_alert("Timeout while waiting GMC workers, \
				it takes more than %d sec\n",
				GMC_WORKER_TIMEOUT_MS);
			kbase_gmc_unlock_task(&gmc_tsk, arg.op);
			return -ETIMEDOUT;
		}
	}

	if(arg.op == GMC_DECOMPRESS)
		kctx->set_pt_flag = false;
	kbase_trace_gpu_mem_usage(kctx->kbdev, kctx);
	kbase_gmc_unlock_task(&gmc_tsk, arg.op);
	n_workers_failed = atomic_read(&n_gmc_workers_failed);
	if (n_workers_failed > 0) {
		pr_warn("%d workers has failed to complete\n", n_workers_failed);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * kbase_get_compressed_region() - Decompress a specific range within a region.
 * @reg:  The memory region.
 * @vpfn: The starting virtual page frame number of the range to decompress.
 * @nr:   The number of pages in the range.
 *
 * This function is an entry point for on-demand decompression, typically called
 * when the system faults on a compressed page. It's a wrapper around
 * kbase_gmc_decompress_region.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int kbase_get_compressed_region(struct kbase_va_region *reg, u64 vpfn, size_t nr)
{
	return kbase_gmc_decompress_region(reg, vpfn, nr);
}

/**
 * kbase_get_compressed_alloc() - Decompress a specific range within an allocation.
 * @alloc:     The memory allocation.
 * @start_idx: The starting index within the allocation's page array.
 * @nr:        The number of pages to decompress.
 *
 * This function is another entry point for on-demand decompression. It's a
 * wrapper around kbase_gmc_decompress_alloc.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int kbase_get_compressed_alloc(struct kbase_mem_phy_alloc *alloc, u64 start_idx, size_t nr)
{
	return kbase_gmc_decompress_alloc(alloc, start_idx, nr);
}

/**
 * kbase_gmc_walk_device() - Find and process contexts on a device.
 * @kbdev: The kbase device to scan.
 * @pid:   The target process ID, or GMC_HANDLE_ALL_KCTXS to process all.
 * @arg:   The GMC arguments (operation and size).
 *
 * Iterates through the list of active contexts on a given Mali device.
 * If a context belongs to the target PID (or if all contexts are being
 * processed), it calls kbase_gmc_walk_kctx() to perform the GMC operation
 * on that context.
 *
 * Return: 0 on success, or the error code from a failed kbase_gmc_walk_kctx call.
 */
int kbase_gmc_walk_device(struct kbase_device *kbdev, pid_t pid, struct kbase_gmc_arg arg)
{
	int ret = 0;
	struct kbase_context *kctx;
	mutex_lock(&kbdev->kctx_list_lock);
	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
        if(kctx !=NULL) {
			if (kctx->tgid == pid || pid == GMC_HANDLE_ALL_KCTXS) {
				ret = kbase_gmc_walk_kctx(kctx, arg);
				if (ret)
					goto out;
			}
        }else{
			pr_warn("kctx is NULL \n");
		}
	}
out:
	mutex_unlock(&kbdev->kctx_list_lock);
	return ret;
}

/**
 * kbase_gmc_compress() - Public API function to trigger compression.
 * @pid:           The process ID to compress, or -1 for all processes.
 * @gmc_dev:       The generic GMC device.
 * @compress_size: The amount of data to compress in megabytes.
 *
 * This is an entry point exposed by the GMC framework. It finds the appropriate
 * kbase_device and calls kbase_gmc_walk_device() to start the compression
 * process.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int kbase_gmc_compress(pid_t pid, struct gmc_device *gmc_dev,long compress_size)
{
	struct kbase_device *kbdev = container_of(gmc_dev, struct kbase_device, kbase_gmc_device);
	struct kbase_gmc_arg arg;
      arg.op = GMC_COMPRESS;
      arg.compress_size = compress_size;

	return kbase_gmc_walk_device(kbdev, pid, arg);
}

/**
 * kbase_gmc_decompress() - Public API function to trigger decompression.
 * @pid:     The process ID to decompress, or -1 for all processes.
 * @gmc_dev: The generic GMC device.
 *
 * This is an entry point exposed by the GMC framework. It finds the appropriate
 * kbase_device and calls kbase_gmc_walk_device() to start the decompression
 * process for all memory associated with the target PID(s).
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int kbase_gmc_decompress(pid_t pid, struct gmc_device *gmc_dev)
{
	struct kbase_device *kbdev = container_of(gmc_dev, struct kbase_device, kbase_gmc_device);
	struct kbase_gmc_arg arg;
	arg.op = GMC_DECOMPRESS;
	arg.compress_size = 0;

	return kbase_gmc_walk_device(kbdev, pid, arg);
}

/**
 * kbase_gmc_meminfo_open() - File operation to open the debugfs file.
 * @in:   The inode.
 * @file: The file object.
 *
 * This function is part of the file_operations structure for the debugfs
 * entry. It hooks up the gmc_memory_info_show() function with the kernel's
 * single-file/seq_file interface, allowing users to read the file.
 *
 * Return: Result of single_open_size.
 */
int kbase_gmc_meminfo_open(struct inode *in, struct file *file)
{
	return single_open_size(file, gmc_memory_info_show, NULL,GPU_MEMORY_SEQ_BUF_SIZE);
}
