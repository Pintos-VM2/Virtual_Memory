#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

enum mmap_status {
    MMAP_OK = 0,
    MMAP_ERR_INVALID,
    MMAP_ERR_CONFLICT,
    MMAP_ERR_FILE,
};

struct file_page {
	size_t page_read_bytes;
	size_t page_zero_bytes;
	struct file *file;
	off_t ofs;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
enum mmap_status do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset, void **ret_addr);
void do_munmap (void *va);
#endif
