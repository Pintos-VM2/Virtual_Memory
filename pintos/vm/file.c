/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"

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

struct file_init_arg {
	size_t remain_byte;
	struct file *file; 
	off_t offset;
};

/* The initializer of file vm */
/* 마지막 페이지 남는 공간은 0으로 채우고, 나중에 file-back할 때 해당 공간은 file에 넣으면 안된다 */
void
vm_file_init(void) {
}

static bool
file_init(struct page *page, void *aux){

	// struct file_init_arg *arg = aux;
	// size_t remain_byte = arg->remain_byte;
	// struct file *file = arg->file;
	// off_t offset = arg->offset;

	// if(remain_byte){

	// 	//if (file_read_at (file, kpage, page_read_bytes, ofs) != (int) page_read_bytes)
		
	// }
	// else{

	// 	if(file_read_at (file, page->frame->kva, PGSIZE, offset) != (int) PGSIZE)
	// 		// 실패시 free 처리 추가?
	// 		return false;
	// }

	return true;
} 


/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void*
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {

	/* file의 offset 부터 length byte를 addr에 매핑한다 */
	/* 마지막 페이지 남는 공간은 0으로 채우고, 나중에 file-back할 때 해당 공간은 file에 넣으면 안된다 */

	struct file_init_arg *arg = malloc(sizeof(struct file_init_arg));
	arg->file = file;

	int rp_max = length / PGSIZE;
	for(int i = 0; i <= rp_max; i++){
		arg->offset = offset+(i*PGSIZE);
		arg->remain_byte = length - (i*PGSIZE) > PGSIZE ? 0 : length - (i*PGSIZE);
		if(!vm_alloc_page_with_initializer(VM_FILE, addr+(i*PGSIZE), writable, file_init, arg))
			return false;
	}
	return true;
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
