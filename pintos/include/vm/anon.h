#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "bitmap.h"

struct page;
enum vm_type;

struct anon_page {
    enum vm_type type;
    size_t swap_slot_idx;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
