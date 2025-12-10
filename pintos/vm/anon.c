/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
struct bitmap *swap_table;

static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

#define SECTORS_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)

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
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	if (swap_disk == NULL)
		PANIC("swap disk not found");

	size_t slot_cnt = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(slot_cnt);
	if (swap_table == NULL)
		PANIC("swap table alloc failed");
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	/* anon_page 정보 세팅 */
	struct anon_page *anon_page = &page->anon;
	anon_page->type = type;
	anon_page->swap_slot = BITMAP_ERROR;

	/* IS_STACK 이면 stack setting */
	if (type & IS_STACK)
		memset(kva, 0, PGSIZE);

	/* todo : 그냥 ANON이면 추가 */

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	size_t slot = anon_page->swap_slot;
	if (slot == BITMAP_ERROR)
		return false;
	
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
		disk_read(swap_disk, slot * SECTORS_PER_PAGE + i,
				  kva + DISK_SECTOR_SIZE * i);

	bitmap_reset(swap_table, slot);

	anon_page->swap_slot = BITMAP_ERROR;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	size_t slot = bitmap_scan_and_flip(swap_table, 0, 1, false);

	if (slot == BITMAP_ERROR)
		return false;

	for (int i = 0; i < SECTORS_PER_PAGE; i++)
		disk_write(swap_disk, slot * SECTORS_PER_PAGE + i,
				   page->frame->kva + DISK_SECTOR_SIZE * i);
	
	anon_page->swap_slot = slot;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (anon_page->swap_slot != BITMAP_ERROR)
		bitmap_reset(swap_table, anon_page->swap_slot);
	
	if(page->frame){
		pml4_clear_page(page->frame->pml4, page->va);
		list_remove(&page->frame->frame_elem);
		palloc_free_page(page->frame->kva);
		free(page->frame); 
	}
}
