/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

#define SECTOR_UNIT 8 //(PGSIZE / DISK_SECTOR_SIZE)
struct bitmap *swap_bm;

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
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
	/* Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	/* swap slot 관리 bitmap 세팅 */
	size_t swap_slot_cnt = disk_size(swap_disk) * DISK_SECTOR_SIZE / PGSIZE;
	swap_bm = bitmap_create(swap_slot_cnt);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	/* anon_page 초기 세팅 */
	anon_page->type = type;

	if (type & IS_STACK)	// IS_STACK 이면 stack setting
		memset(kva, 0, PGSIZE);
	
	anon_page->swap_slot_idx = BITMAP_ERROR;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	//idx 가져오기
	size_t idx = anon_page->swap_slot_idx;
	if(idx == BITMAP_ERROR) return false;
	
	//idx로부터 kva로 disk_read
	size_t start_sector = idx * SECTOR_UNIT;
	for (int i = 0; i < SECTOR_UNIT; i++) {
		disk_read(swap_disk, start_sector + i, kva + (i * DISK_SECTOR_SIZE));
	}
	//bitmap 0으로 만들고 anon_page idx update
	bitmap_set(swap_bm, idx, false);
	anon_page->swap_slot_idx = BITMAP_ERROR;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
/* page와 연결된 frame swap_disk에 기록 */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	//swap_disk에서 빈 공간 찾기
	size_t idx = bitmap_scan_and_flip(swap_bm, 0, 1, 0);
	if(idx == BITMAP_ERROR) return false;

	// 해당 공간에 disk_write, idx 기록
	void *kva = page->frame->kva;
	size_t start_sector = idx * SECTOR_UNIT;
    for (int i = 0; i < SECTOR_UNIT; i++) {
		disk_write(swap_disk, start_sector + i, kva + (i * DISK_SECTOR_SIZE));
    }
	anon_page->swap_slot_idx = idx;

	//pml4 매핑 해제(va)
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
