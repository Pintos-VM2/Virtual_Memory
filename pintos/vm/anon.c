/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/malloc.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_bitmap;

static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	swap_disk = disk_get(1, 1);

    if (swap_disk == NULL) {
        PANIC("no swap disk");
    }

    size_t total_sectors = disk_size(swap_disk);
    size_t slot_cnt = total_sectors / SECTORS_PER_PAGE;
    swap_bitmap = bitmap_create (slot_cnt);
    if (swap_bitmap == NULL) {
        PANIC("cannot create swap bitmap");
    }

    bitmap_set_all (swap_bitmap, false);  // false = free
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->type = type;
	anon_page->swap_slot = SIZE_MAX;

	if (type & IS_STACK)
		memset(kva, 0, PGSIZE);

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	if(anon_page->swap_slot == SIZE_MAX){
		return false;
	}
    size_t idx = anon_page->swap_slot;

    for (int i = 0; i < SECTORS_PER_PAGE; i++) {
        disk_read (swap_disk,
                    idx * SECTORS_PER_PAGE + i,
                    (uint8_t *)kva + i * BLOCK_SECTOR_SIZE);
    }

	bitmap_reset(swap_bitmap, idx);
	anon_page->swap_slot = SIZE_MAX;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	// 빈 스왑 슬롯 찾기
    size_t idx = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
    if (idx == BITMAP_ERROR) {
        PANIC("swap is full");
    }
    anon_page->swap_slot = idx;

	void *kva = page->frame->kva;

	// block size 별로 disk write 기능 필요
	for (int i = 0; i < SECTORS_PER_PAGE; i++) {
        disk_write (swap_disk,
                     idx * SECTORS_PER_PAGE + i,
                     (uint8_t *)kva + i * BLOCK_SECTOR_SIZE);
    }

	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	if(page->frame){
		list_remove(&page->frame->frame_elem);
		palloc_free_page(page->frame->kva);
		free(page->frame);
	}

	pml4_clear_page(thread_current()->pml4, page->va);
}
