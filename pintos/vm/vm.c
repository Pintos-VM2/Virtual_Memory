/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/threads/mmu.h"
#include <string.h>


/*
  1. 프로세스가 가상 주소에 접근
  2. SPT에서 그 가상 주소에 해당하는 page 정보를 찾음
  3. Page가 메모리에 없으면 (page fault 발생)
  4. Frame을 할당받아서 페이지를 로드
  5. Page Table(pml4)에 매핑 추가 (VA → PA)
*/
static void page_destructor (struct hash_elem *e, void *aux UNUSED);
unsigned page_hash_func(const struct hash_elem *e, void *aux);
bool page_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);


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

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va ) {
	struct page *page = NULL;
	
	if(!hash_empty(&spt->spt_hash)){
		struct page tmp_page = {0};
		tmp_page.va = va;
		struct hash_elem *tmp_elem = hash_find(&spt->spt_hash,  &tmp_page.hash_elem);

		if(tmp_elem != NULL){
			page = hash_entry(tmp_elem, struct page, hash_elem);
		}
	}

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page ) {
	int succ = false;
	
	struct hash_elem *tmp_elem = hash_insert(&spt->spt_hash, &page->hash_elem);
	
	if(tmp_elem == NULL){
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	ASSERT(page != NULL);  // page가 NULL이면 패닉!
	ASSERT(spt != NULL);   // spt가 NULL이면 패닉!

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

/* 물리 페이지를 가져와서 frame 구조체로 포장해주는 것*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	void *tmp_kva;
	
	frame = malloc(sizeof(struct frame));
	ASSERT (frame != NULL);

	tmp_kva = palloc_get_page(PAL_USER);
	if(tmp_kva == NULL){
		vm_evict_frame();
		tmp_kva = palloc_get_page(PAL_USER);
	}

	frame->kva = tmp_kva;
	frame->page = NULL;
	ASSERT (frame->page == NULL);
	
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

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
	struct page *page = NULL;
	struct thread *current_thread = thread_current();

	// spt는 current thread에 있다!!~
	page = spt_find_page(&current_thread -> spt, va);

	if (page == NULL) {
		return false;
	}  

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// 물리 페이지를 가져와서 frame
	struct frame *frame = vm_get_frame ();
	struct thread *current = thread_current ();

	// 프레임의 페이지(가상 주소)는 전달 받은 값, 페이지의 frame은 vm_get_frame값
	frame->page = page;
	page->frame = frame;

	// 페이지 테이블과 프레임 set
	// TODO.. writable을 어떻게 처리할지에 대해서 확인 필요
	if(!pml4_set_page(current->pml4, page->va, page->frame->kva, true)){
		return false;
	}

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init(struct supplemental_page_table *spt) {
	hash_init(&spt->spt_hash, (hash_hash_func *)page_hash_func, (hash_less_func *)page_less_func, NULL);
}

bool
supplemental_page_table_copy (struct supplemental_page_table *dst , 
	struct supplemental_page_table *src) {
	struct hash_iterator i;

	if (src == NULL){
		return false;
	}

	if (dst != NULL){
		hash_init (&dst->spt_hash, (hash_hash_func *)page_hash_func, (hash_less_func *)page_less_func, NULL);
	}

	hash_first(&i, &src->spt_hash);
	
	while (hash_next(&i) != NULL) {
		struct hash_elem *e = hash_cur(&i);
		struct page *page = hash_entry(e, struct page, hash_elem);
		hash_insert(&dst->spt_hash, &page->hash_elem);
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	if (spt == NULL){
		return;
	}

	hash_destroy(&spt->spt_hash, (hash_action_func *)page_destructor);
}

static void
page_destructor (struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(page); 
}

unsigned page_hash_func(const struct hash_elem *e, void *aux){
	const struct page *p = hash_entry(e, struct page, hash_elem);

	// page의 va(가상 주소)를 해시값으로 변환
	return hash_bytes(&p->va, sizeof(p->va));

}

bool page_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux){
	struct page *ta = hash_entry(a, struct page, hash_elem);
	struct page *tb = hash_entry(b, struct page, hash_elem);
	return ta->va < tb->va;
}