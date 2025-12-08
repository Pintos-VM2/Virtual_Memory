/* file.c: Implementation of memory backed file object (mmaped object). */

#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/vm.h"
#include "threads/malloc.h"
#include "lib/round.h"
#include <string.h>

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static void rollback_mmap(void *addr, void *page, struct file *file);
static bool read_file(struct file_page *file_page, void *kva);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* operations를 설정하기 전에 uninit에서 aux를 추출해야 함
	   (union이므로 operations 설정 시 uninit이 오버라이트됨) */
	struct load_segment_arg *aux = (struct load_segment_arg *)page->uninit.aux;

	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	// aux가 NULL이면 fork 중이므로 데이터는 나중에 memcpy
	if (aux == NULL) {
		file_page->file = NULL;
		file_page->ofs = 0;
		file_page->page_read_bytes = 0;
		file_page->page_zero_bytes = 0;
		return true;
	}

	// aux에서 파일 정보를 가져와서 file_page에 저장
	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->page_read_bytes = aux->page_read_bytes;
	file_page->page_zero_bytes = aux->page_zero_bytes;

	// 파일에서 데이터 읽기 (첫 번째 page fault 시 수행)
	read_file(file_page, kva);

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;

	/* 파일에서 데이터 읽기 */
	read_file(file_page, kva);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;

	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(file_page->file, page->frame->kva,
					  file_page->page_read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}
	// clean필요
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;

	// Dirty 페이지면 파일에 write back
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(file_page->file, page->frame->kva,
					  file_page->page_read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}
}

/* Do the mmap */
enum mmap_status
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset, void **ret_addr) {
	size_t file_len = file_length(file);
	size_t map_bytes;
	uint32_t read_bytes, zero_bytes;

	if (length == 0){
		return MMAP_ERR_INVALID;
	}

	if(addr ==0 || addr == NULL || pg_round_down(addr) != addr){
		return MMAP_ERR_INVALID;
	}

	if(file_len == 0){
		return MMAP_ERR_FILE;
	}

	if (offset < 0 || (size_t)offset >= file_len) {
		return MMAP_ERR_INVALID;
	}

	size_t remain = file_len - offset;
	map_bytes = remain < length ? remain : length;

	for (void *page = addr; page < (uint8_t *)addr + ROUND_UP(map_bytes, PGSIZE); page += PGSIZE) {
		if (spt_find_page(&thread_current()->spt, page) != NULL) {
			return MMAP_ERR_CONFLICT;
		}
	}

	struct file *file_copy = file_reopen(file);
	if (file_copy == NULL)
		return MMAP_ERR_FILE;

	for (void *page = addr; page < addr + map_bytes; page += PGSIZE) {
		size_t bytes_left = map_bytes - (page - addr);
		struct load_segment_arg *arg = malloc(sizeof(struct load_segment_arg));

		if(arg == NULL) {
			rollback_mmap(addr, page, file_copy);
			return MMAP_ERR_FILE;
		}

		read_bytes = bytes_left >= PGSIZE ? PGSIZE : bytes_left;
		zero_bytes = PGSIZE - read_bytes;

		arg->file = file_copy;
		arg->ofs = offset + (page - addr);
		arg->page_read_bytes = read_bytes;
		arg->page_zero_bytes = zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_FILE, page, writable, NULL, arg)){
			free(arg);
			rollback_mmap(addr, page, file_copy);
			return MMAP_ERR_FILE;
		}
	}

	struct mmap_file *mmap_file = malloc(sizeof(struct mmap_file));
	if (mmap_file == NULL) {
		rollback_mmap(addr, NULL, file_copy);
		return MMAP_ERR_FILE;
	}

	mmap_file->start = addr;
	mmap_file->end = (uint8_t *)addr + ROUND_UP(map_bytes, PGSIZE);
	mmap_file->file = file_copy;
	mmap_file->length = map_bytes;

	list_push_back(&thread_current()->mmap_list, &mmap_file->elem);
	*ret_addr = addr;

	return MMAP_OK;
}
/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *cur = thread_current();
	struct list_elem *e;

	// mmap_list에서 addr로 시작하는 영역 찾기
	for (e = list_begin(&cur->mmap_list); e != list_end(&cur->mmap_list); e = list_next(e)) {
		struct mmap_file *mf = list_entry(e, struct mmap_file, elem);

		if (mf->start == addr) {
			// 이제 해제
			rollback_mmap(mf->start, mf->end, mf->file);
			list_remove(&mf->elem);
			free(mf);
			return;
		}
	}
}

static bool
read_file(struct file_page *file_page, void *kva){
	// 파일에서 데이터 읽기
	if (file_read_at(file_page->file, kva, file_page->page_read_bytes, file_page->ofs)
		!= (int)file_page->page_read_bytes) {
		return false;
	}

	// 나머지 부분을 0으로 채우기
	memset(kva + file_page->page_read_bytes, 0, file_page->page_zero_bytes);
	
	return true;
}

/* rollback하여 할당 해제 전문 함수*/
static void
rollback_mmap(void *addr, void *page, struct file *file){
	for (void *p = addr; p < page; p += PGSIZE) {
		struct page *allocated_page = spt_find_page(&thread_current()->spt, p);
		if (allocated_page != NULL) {
			spt_remove_page(&thread_current()->spt, allocated_page);
		}
	}
	file_close(file);
}