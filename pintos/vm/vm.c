/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "threads/mmu.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "lib/kernel/list.h"

static void page_destructor (struct hash_elem *e, void *aux UNUSED);
/* Global frame list for eviction */
struct list frame_list;
static struct list_elem *clock_hand;

/* Hash function for supplemental page table */

static uint64_t
hash_page (const struct hash_elem *e, void *aux UNUSED) {
    const struct page *p = hash_entry(e, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

static bool
less_page (const struct hash_elem *a,
           const struct hash_elem *b,
           void *aux UNUSED) {
    const struct page *pa = hash_entry(a, struct page, hash_elem);
    const struct page *pb = hash_entry(b, struct page, hash_elem);
    return pa->va < pb->va;
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* if(upage가 이미 할당 됐는지) 확인 */
	if (spt_find_page (spt, upage) == NULL) {
		/* type 인자에 따라 initializer를 선택하고, 이를 인자로 uninit_new를 호출 */
		struct page *page = malloc(sizeof(struct page));
		if(page == NULL)
			goto err;

		switch(VM_TYPE(type)){
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break;

			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);
				break;

			default:
				PANIC(" DEBUG : vm_alloc_page_initializer undefine type error!!!!! ");
		}
		/* uninit_new 호출 후 나머지 field 채우기 */
		page->writable = writable;

		if(!spt_insert_page(spt, page)){
			free(page);
			goto err;
		}
		page->pml4 = thread_current()->pml4;

		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page temp_page;
    struct hash_elem *e;

    va = pg_round_down (va);
    temp_page.va = va;

    e = hash_find(&spt->hash, &temp_page.hash_elem);

    if (e == NULL)
        return NULL;

    return hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	return hash_insert(&spt->hash, &page->hash_elem) == NULL;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->hash, &page->hash_elem);
	vm_dealloc_page (page);
	return;
}

/* Get the struct frame, that will be evicted. */
/* LRU clock 알고리즘, frame list에서 하나 꺼내서 전달 */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;

	if (clock_hand == NULL || clock_hand == list_end(&frame_list))
        clock_hand = list_begin(&frame_list);

    while (true) {
        struct frame *f = list_entry(clock_hand, struct frame, frame_elem);
        if (!f->no_victim) {
            if (!pml4_is_accessed(f->page->pml4, f->page->va)) {
                clock_hand = list_next(clock_hand);
				victim = f;
                break;
            }
            pml4_set_accessed(f->page->pml4, f->page->va, false);
        }

        clock_hand = list_next(clock_hand);
        if (clock_hand == list_end(&frame_list))
            clock_hand = list_begin(&frame_list);
    }

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
/* frame 받아서 swap_out 처리하고, 연결 끊어서 다시 사용 */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();

	if(!swap_out(victim->page))
		PANIC("DEBUG : swap disk is full");

	// page <-> frame 연결 끊기
    victim->page->frame = NULL;
    victim->page = NULL;

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* 물리 메모리 확보(frame 확보), 
   남은 user pool 없으면 vm_evict_frame()으로 받아옴 */
static struct frame *
vm_get_frame (void) {

	void *kpage = palloc_get_page(PAL_USER);
	if (kpage == NULL){
		return vm_evict_frame();
	}

	struct frame *f = malloc(sizeof(struct frame));
	if (f == NULL) {
		palloc_free_page(kpage);
		PANIC("memory allocation failed");
	}
		
	f->kva = kpage;
	f->page = NULL;
	list_push_back(&frame_list, &(f->frame_elem));

	return f;
}

/* Growing the stack. */
/* caller가 claim 함 */
static bool
vm_stack_growth (void *addr) {
	return vm_alloc_page(VM_ANON | IS_STACK, pg_round_down(addr), true);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {

	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	void *rsp;

	if(addr == NULL || is_kernel_vaddr(addr)) 
		return false;	

	if(!not_present) 
		return false;

	rsp = user ? f->rsp : curr->user_rsp;

	struct page *page = spt_find_page(spt, addr);
	if(page == NULL){

		if(addr < (rsp - 8) || !is_stack_vaddr(addr))
			return false;

		if(!vm_stack_growth(addr))
			return false;

		page = spt_find_page(spt, addr);
		if(page == NULL) 
			return false;
	}

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame();
    if (frame == NULL)
        goto error;

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* VA → KVA 매핑 */
    if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
        goto error;

	/* 페이지 내용 채우기 (lazy load / swap-in) */
    if (!swap_in(page, frame->kva))
        goto error;

    return true;

error:
    /* 링크 되돌리기 */
    page->frame = NULL;
    frame->page = NULL;

    /* frame 자원 회수 */
    palloc_free_page(frame->kva);
    free(frame);

    return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->hash, hash_page, less_page, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) {

	if (src == NULL) return false;

	struct hash_iterator i;
	struct file *exec_file = thread_current()->execute_file;
	hash_first(&i, &src->hash);

	while (hash_next(&i) != NULL) {
		struct hash_elem *e = hash_cur(&i);
		struct page *p_page = hash_entry(e, struct page, hash_elem);

		struct uninit_page p_uninit = p_page->uninit;
		struct file_load_arg *p_aux = p_uninit.aux;
		enum vm_type type = page_get_type(p_page);

		/* ops->type 확인 */
		switch(p_page->operations->type){
			case VM_UNINIT:
				struct file_load_arg *c_aux = malloc(sizeof(struct file_load_arg));
				memcpy(c_aux, p_aux, sizeof(struct file_load_arg));

				/* type에 따라 file 연결 */
				if(type == VM_ANON)
					c_aux->file = exec_file;
				else if(type == VM_FILE)
					c_aux->file = file_reopen(p_aux->file); //lock 보류

				if(!vm_alloc_page_with_initializer(p_uninit.type, p_page->va, p_page->writable, p_uninit.init, c_aux))
					return false;
				//claim 안함. 부모도 fault 대기중
				break;

			case VM_ANON:
			case VM_FILE:
				if(!vm_alloc_page(p_page->operations->type, p_page->va, p_page->writable))
					return false;
				if(!vm_claim_page(p_page->va))
					return false;
				struct page *c_page = spt_find_page(&thread_current()->spt, p_page->va);
				memcpy(c_page->frame->kva, p_page->frame->kva, PGSIZE);
				break;

			default:
				return false;
		}	
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	if (spt == NULL) return;
	hash_destroy(&spt->hash, (hash_action_func *)page_destructor);
}

static void
page_destructor (struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(page); 
}