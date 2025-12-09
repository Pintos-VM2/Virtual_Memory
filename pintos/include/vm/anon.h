#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "devices/disk.h"

#define BLOCK_SECTOR_SIZE 512

struct page;
enum vm_type;

struct anon_page {
    enum vm_type type;
    disk_sector_t swap_slot;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
