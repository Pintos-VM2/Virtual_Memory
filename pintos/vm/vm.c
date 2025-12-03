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
	vm_dealloc_page (page);
	return;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {

	void *kpage = palloc_get_page(PAL_USER);
	if (kpage == NULL)
		PANIC("to do");

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

bool stack_init (struct page *page, void *aux){
	/* 일단 zero-fill 정도 */
	memset(page->frame->kva, 0, PGSIZE);
	return true;
}

/* Growing the stack. */
/* caller가 claim 함 */
static bool
vm_stack_growth (void *addr UNUSED) {

	void *va = pg_round_down(addr);
	/* stack 크기 제한 초과 */
	if(va < MIN_USER_STACK) return false;

	if(!vm_alloc_page_with_initializer(VM_ANON | IS_STACK, va, true, stack_init, NULL))
		return false;

	return true;
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

	/* Validate the fault */
	if(addr == NULL || is_kernel_vaddr(addr))
		return false;	

	//0 이면 이상한 접근(1이면 물리 페이지 메핑X)
	if(!not_present)
		return false;

	void *user_rsp = user ? f->rsp : thread_current()->user_rsp;

	struct page *page = spt_find_page(spt, addr);
	if(page == NULL){

		if(addr < (user_rsp - 8) || addr > USER_STACK)
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
supplemental_page_table_copy (struct thread *child , struct thread *parent) {
	
	struct supplemental_page_table *dst = &child->spt;
	struct supplemental_page_table *src = &parent->spt;

	struct hash_iterator i;

	if (src == NULL)
		return false;

	hash_first(&i, &src->hash);

	struct thread *dup_file = file_duplicate(parent->execute_file);
	if(dup_file == NULL)
		return false;
	child->execute_file = dup_file;

	while (hash_next(&i) != NULL) {
		struct hash_elem *e = hash_cur(&i);
		struct page *p_page = hash_entry(e, struct page, hash_elem);

		struct uninit_page p_uninit = p_page->uninit;
		struct load_segment_arg *p_aux = p_uninit.aux;

		/* ops->type 확인 */
		switch(p_page->operations->type){
			case VM_UNINIT:
				struct load_segment_arg *c_aux = malloc(sizeof(struct load_segment_arg));
				memcpy(c_aux, p_aux, sizeof(struct load_segment_arg));
				c_aux->file = dup_file;

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
