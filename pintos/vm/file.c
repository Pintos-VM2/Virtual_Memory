/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/syscall.h" /* filesys_lock */

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

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
	/* Set up the handler */
	struct file_load_arg *aux = page->uninit.aux;

	page->file.file = aux->file;
	page->file.ofs = aux->ofs;
	page->file.read_bytes = aux->page_read_bytes;
	page->file.zero_bytes = aux->page_zero_bytes;

	lock_acquire(&filesys_lock);
	off_t read = file_read_at(page->file.file, kva, page->file.read_bytes, page->file.ofs);
	lock_release(&filesys_lock);

	free(aux);
	if (read != (off_t)page->file.read_bytes)
		return false;

	memset(kva + page->file.read_bytes, 0, page->file.zero_bytes);
	page->operations = &file_ops;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page = &page->file;

	lock_acquire(&filesys_lock);
	off_t read = file_read_at(file_page->file, kva, file_page->read_bytes, file_page->ofs);
	lock_release(&filesys_lock);

	if (read != (off_t)file_page->read_bytes)
		return false;

	memset(kva + file_page->read_bytes, 0, file_page->zero_bytes);
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *cur = thread_current();

	if (pml4_is_dirty(cur->pml4, page->va) && page->frame != NULL) {
		lock_acquire(&filesys_lock);
		file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
		lock_release(&filesys_lock);
		pml4_set_dirty(cur->pml4, page->va, false);
	}

	/* swap 장치가 없으므로 매핑만 끊고 프레임 연결은 상위에서 정리 */
	pml4_clear_page(cur->pml4, page->va);
	page->frame = NULL;

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct file *dup_file = file_reopen(file);
	if (dup_file == NULL)
		return NULL;

	size_t pages = DIV_ROUND_UP(length, PGSIZE);
	size_t map_len = pages * PGSIZE;

	struct thread *cur = thread_current();
	struct supplemental_page_table *spt = &cur->spt;

	for (size_t idx = 0; idx < map_len; idx += PGSIZE) {
		if (spt_find_page(spt, addr + idx)) {
			file_close(dup_file);
			return NULL;
		}
	}

	lock_acquire(&filesys_lock);
	off_t file_len = file_length(dup_file);
	lock_release(&filesys_lock);

	size_t written = 0;
	while (written < map_len) {
		off_t current_ofs = offset + (off_t)written;
        size_t file_remain = (current_ofs < file_len) ? (size_t)(file_len - current_ofs) : 0;
        
        size_t map_remain = (written < length) ? length - written : 0;

        size_t page_read = (file_remain < PGSIZE) ? file_remain : PGSIZE;
        
        if (page_read > map_remain)
             page_read = map_remain;

        size_t page_zero = PGSIZE - page_read;

        struct file_load_arg *aux = malloc(sizeof *aux);
        if (!aux)
            break;

        aux->page_read_bytes = page_read;
        aux->page_zero_bytes = page_zero;
		aux->file = dup_file;
		aux->ofs = current_ofs;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr + written,
                writable, NULL, aux)) {
            free(aux);
            break;
        }

        written += PGSIZE;
	}
	if (written == map_len) {
		struct mmap_args *args = malloc(sizeof *args);
		if (args)
		{
			args->vaddr = addr;
			args->page_count = pages;
			args->file = dup_file;
			list_push_back(&cur->mmap_list, &args->elem);
			return addr;
		}
	}

	for (size_t idx = 0; idx < written; idx += PGSIZE) {
		struct page *p = spt_find_page(spt, addr + idx);
		if (p)
			spt_remove_page(spt, p);
	}

	lock_acquire(&filesys_lock);
	file_close(dup_file);
	lock_release(&filesys_lock);

	return NULL;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *cur = thread_current();
    struct list *mmap_list = &cur->mmap_list;
    struct list_elem *e;
    struct mmap_args *target_args = NULL;

    for (e = list_begin(mmap_list); e != list_end(mmap_list); e = list_next(e)) {
        struct mmap_args *args = list_entry(e, struct mmap_args, elem);
        if (args->vaddr == addr) {
            target_args = args;
            break;
        }
    }

    if (target_args == NULL)
		return;

    void *chk_addr = target_args->vaddr;
    for (size_t i = 0; i < target_args->page_count; i++) {
        struct page *page = spt_find_page(&cur->spt, chk_addr);
        
        if (page) {
            if (pml4_is_dirty(cur->pml4, page->va)) {
                lock_acquire(&filesys_lock);
                file_write_at(target_args->file, page->frame->kva, page->file.read_bytes, page->file.ofs);
                lock_release(&filesys_lock);
                pml4_set_dirty(cur->pml4, page->va, false);
			}
            spt_remove_page(&cur->spt, page);
        }
        chk_addr += PGSIZE;
    }

    lock_acquire(&filesys_lock);
    file_close(target_args->file);
    lock_release(&filesys_lock);

    list_remove(&target_args->elem);
    free(target_args);
}
