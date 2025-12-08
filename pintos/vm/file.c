/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <stdlib.h>

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

static bool file_init(struct page *page, void *aux);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init(void) {
	/* lock 추가? */
}

/* 마지막 페이지 남는 공간은 0으로 채우고, 나중에 file-back할 때 해당 공간은 file에 넣으면 안된다 */
static bool
file_init(struct page *page, void *aux){

	struct file_load_arg *arg = aux;
	struct file *file = arg->file;
	off_t ofs = arg->ofs;
	size_t page_read_bytes = arg->page_read_bytes;

	/* 할당받은 페이지에 파일 내용을 읽어 채운다. */
	void *kpage = page->frame->kva;
	size_t read_bytes = file_read_at (file, kpage, page_read_bytes, ofs);
	size_t page_zero_bytes = PGSIZE - read_bytes;
	memset (kpage + read_bytes, 0, page_zero_bytes);

	//file_page 구조체 데이터 저장
	if(arg->is_last)
		page->file.is_last = true;
	page->file.page_read_bytes = read_bytes;
	page->file.file = file; //reopen된 page 고유 file
	page->file.ofs = ofs;

	free(arg);

	return true;
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type UNUSED, void *kva UNUSED) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;

	// file_page 초기화
	file_page->file = NULL;
	file_page->is_last = false;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;

	struct file *file = file_page->file;
	off_t ofs = file_page->ofs;
	size_t page_read_bytes = file_page->page_read_bytes;

	size_t read_bytes = file_read_at (file, kva, page_read_bytes, ofs);
	size_t page_zero_bytes = PGSIZE - read_bytes;
	memset (kva + read_bytes, 0, page_zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;

	/* page 정리 */
	write_back(page);

	if(file_page->file)
		file_close(file_page->file);

	if(page->frame){
		list_remove(&page->frame->frame_elem);
		palloc_free_page(page->frame->kva);
		free(page->frame);
	}

	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
void*
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {

	/* 검증은 s_mmap(호출자)에서 함 */
	/* file의 offset 부터 length byte를 addr에 매핑한다 */

	int rp_max = length / PGSIZE;
	for(int i = 0; i <= rp_max; i++){

		size_t page_read_bytes = length - (i*PGSIZE) > PGSIZE ? PGSIZE : length - (i*PGSIZE);

		/* arg 세팅 블럭 */
		struct file_load_arg *arg = malloc(sizeof(struct file_load_arg));
		if(arg == NULL) return false;
		arg->ofs = offset+(i*PGSIZE);
		arg->page_read_bytes = page_read_bytes;
		struct file *rfile = file_reopen(file);
		if(rfile == NULL)
			PANIC(" DEBUG : mmap file reopen fail ");
		arg->file = rfile;
		arg->is_last = i == rp_max ? true : false;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr+(i*PGSIZE), writable, file_init, arg)){
			free(arg);
			return false;
		}
	}
	return addr;
}

/* Do the munmap */
/* page 찾아서 mmap으로 할당된 곳이면 is_last 나올때까지 반복
   mmap은 연속적인 공간에 할당하기 때문에 is_last만 찾도록 디자인함*/
void
do_munmap (void *addr) {

	struct thread *curr = thread_current();
	bool last;

	while(true){
		struct page *page = spt_find_page(&curr->spt, addr);
		if(page == NULL)
			PANIC("DEBUG : invalid addr for munmap");

		enum vm_type type = page->operations->type;

		if(type == VM_ANON)
			PANIC("DEBUG : invalid addr type for munmap");

		//FILE이면 last를 file_page에서 찾아옴, UNINIT(FILE 대기)이면 aux에서 찾아옴
		last = (type == VM_FILE) ? page->file.is_last : ((struct file_load_arg*)page->uninit.aux)->is_last;
		spt_remove_page(&curr->spt, page);
		
		if(last)
			break;

		addr += PGSIZE;
	}
}

void
write_back(struct page *page){

	if(!pml4_is_dirty(thread_current()->pml4, page->va))
		return;

	off_t ofs = page->file.ofs;
	size_t read_bytes = page->file.page_read_bytes;
	struct file *file = page->file.file;

	if(file_write_at(file, page->frame->kva, read_bytes, ofs) != (int) read_bytes)
		PANIC("DEBUG : write back 오류 !!! ");
}