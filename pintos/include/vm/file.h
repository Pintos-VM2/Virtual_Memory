#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"
#include "threads/mmu.h"
#include "lib/round.h"

struct page;
enum vm_type;

struct file_page {
	struct file *file;				/* 해당 페이지의 원본 파일 */
	off_t ofs;						/* 파일 내 페이지 시작 오프셋 */
	size_t read_bytes;				/* 이 페이지에 파일에서 실제로 채워 넣을 바이트 수 */
	size_t zero_bytes;				/* 페이지의 나머지를 0으로 채울 바이트 수(PGSIZE - read_bytes) */
};

extern struct lock filesys_lock;

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
