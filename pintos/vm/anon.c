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
	/* Set up the handler */
	page->operations = &anon_ops;

	/* anon_page 정보 세팅 */
	struct anon_page *anon_page = &page->anon;
	anon_page->type = type;

	if (type & IS_STACK)	// IS_STACK 이면 stack setting
		memset(kva, 0, PGSIZE);
	
	anon_page->swap_slot_idx = BITMAP_ERROR;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
// kva일단 할당 되었다고 가정
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	int cnt;

	//idx 가져오기
	size_t idx = anon_page->swap_slot_idx;
	if(idx == BITMAP_ERROR) return false;
	
	//idx로부터 kva로 disk_read
	int i = SECTOR_UNIT;
	while(i--){
		cnt = SECTOR_UNIT-i;
		disk_read(swap_disk, idx+cnt, kva+(cnt*DISK_SECTOR_SIZE));
	}

	//bitmap 0으로 만들고 anon_page update
	bitmap_set_multiple(swap_bm, idx, SECTOR_UNIT, 0);
	anon_page->swap_slot_idx = BITMAP_ERROR;

	//page에 물리 프레임 연결, frame table에 추가, pml4 매핑 비트 on
	page->frame->kva = kva;
	pml4_set_page(thread_current()->pml4, page->va, kva, page->writable);
	list_push_back(&frame_list, &page->frame->frame_elem);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	int cnt;

	//swap_disk에서 빈 공간 찾기
	size_t idx = bitmap_scan_and_flip(swap_bm, 0, SECTOR_UNIT, 0);
	if(idx == BITMAP_ERROR) return false;

	// 해당 공간에 disk_write, idx 기록
	int i = SECTOR_UNIT;
	void *kva = page->frame->kva;
	while(i--){
		cnt = SECTOR_UNIT-i;
		disk_write(swap_disk, idx+cnt, kva+(cnt*DISK_SECTOR_SIZE));
	}
	anon_page->swap_slot_idx = idx;

	//물리 프레임 회수, pml4 매핑 비트 off
	pml4_clear_page(thread_current()->pml4, page->va);
	list_remove(&page->frame->frame_elem);
	palloc_free_page(page->frame->kva);

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	/* anon_page 구조체 만들어지면 free 추가*/

	pml4_clear_page(thread_current()->pml4, page->va);

	list_remove(&page->frame->frame_elem);

	palloc_free_page(page->frame->kva);
}
