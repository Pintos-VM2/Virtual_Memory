#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include <string.h>

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void s_halt(void);
static int s_write (int fd, const void *buffer, unsigned length);
static bool s_create(const char *file, unsigned initial_size);
static void s_exit(int status);
static int s_open(const char *file);
//static void check_valid_access(void *uaddr);
static void s_close(int fd);
static int s_read(int fd, void *buffer, unsigned size);
static int s_filesize(int fd);
//static bool check_buffer(void *buffer, int length);
static int64_t get_user(const uint8_t *uadder);
static bool put_user(uint8_t *udst, uint8_t byte);
static int s_wait(tid_t pid);
static tid_t s_fork(const char *thread_name, struct intr_frame *f);
static int s_exec(const char *cmd_line);
static void s_seek(int fd, unsigned position);
static bool s_remove(const char *file);
static unsigned s_tell(int fd);
static int s_dup2(int oldfd, int newfd);

static void valid_get_addr(void *addr);
static void valid_get_buffer(char *addr, unsigned length);
static void valid_put_addr(char *addr, unsigned length);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

struct lock filesys_lock;


static int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
        "movabsq $done_get, %0\n"
        "movzbq %1, %0\n"
        "done_get:\n"
        : "=&a" (result) : "m" (*uaddr));
    return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	uint64_t syscall_num = f -> R.rax;
	thread_current()->rsp = f -> rsp;
	switch (syscall_num)
	{	
		case SYS_HALT:
			s_halt();
			break;
	
		case SYS_WRITE:
			f->R.rax = s_write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned int)f->R.rdx);
			break;

		case SYS_EXIT:
			s_exit((int) f -> R.rdi);
        	break;

		case SYS_CREATE:
			f -> R.rax = s_create((const char *) f -> R.rdi, (unsigned) f -> R.rsi);
			break;

		case SYS_OPEN:
			f -> R.rax = s_open((const char *) f -> R.rdi);
			break;

		case SYS_CLOSE:
			s_close((int) f -> R.rdi);
			break;

		case SYS_READ:
			f -> R.rax = s_read((int) f -> R.rdi, (void *) f -> R.rsi, (unsigned) f -> R.rdx);
			break;
		
		case SYS_FILESIZE:
			f -> R.rax = s_filesize((int) f -> R.rdi);
			break;
		
		case SYS_WAIT:
			f -> R.rax = s_wait((int) f -> R.rdi);
			break;

		case SYS_FORK:
			f -> R.rax = s_fork((const char *) f -> R.rdi, f);
			break;
		
		case SYS_EXEC:
			f -> R.rax = s_exec((const char *) f -> R.rdi);
			break;

		case SYS_SEEK:
			s_seek((int) f -> R.rdi, (unsigned) f -> R.rsi);
			break;

		case SYS_REMOVE:
			f -> R.rax = s_remove((const char *)f -> R.rdi);
			break;

		case SYS_TELL:
			f -> R.rax = s_tell((int) f -> R.rdi);
			break;

		case SYS_DUP2:
			f -> R.rax = s_dup2((int) f -> R.rdi, f -> R.rsi);
			break;

		default:
			printf("undefined system call! %llu", syscall_num); 
			s_exit(-1);
			break;
	}
}

static bool check_writable(char *buffer, unsigned length) {
	char *start = buffer;
	char *end = buffer + length -1;

	struct page *sp = spt_find_page(&thread_current() -> spt, start);
 	struct page *ep = spt_find_page(&thread_current() -> spt, end);

 	if ((sp != NULL && !sp->writable) || (ep != NULL && !ep->writable))
 		return false;

	return true;
}

/* user 포인터 검사 */
static void valid_get_addr(void *addr){
	if(get_user(addr) < 0)
		s_exit(-1);
}
/* 버퍼에서 가져오기 검사 */
static void valid_get_buffer(char *buffer, unsigned length){

	char *end = buffer + length -1;
	if(get_user(buffer) < 0 || get_user(end) < 0)
		s_exit(-1);
}
/* 버퍼에 쓰기 검사 */
static void valid_put_buffer(char *buffer, unsigned length){
	char *start = buffer;
	char *end = buffer + length -1;

	if(put_user(start, 0) == 0 || put_user(end, 0) == 0)
		s_exit(-1);
}

static void 
s_halt(void){
	power_off();
}

static void
s_exit(int status){
	struct thread *cur = thread_current();
	cur -> exit_status = status;
    thread_exit();
}

static int 
s_write (int fd, const void *buffer, unsigned length){
	valid_get_buffer(buffer, length);
	struct file_descriptor *wrap_fd = get_fd_wrapper(fd);
	struct file *cur_file = NULL;
	int actual_byte_written = 0;

	if(wrap_fd == NULL) return -1;
	enum fd_type f_type = wrap_fd -> type;

	switch (f_type){
		case FD_STDIN:
			actual_byte_written = -1;
			break;

		case FD_STDOUT:
			putbuf(buffer, length);	
			actual_byte_written = (int) length;
			break;

		case FD_FILE:
			cur_file = wrap_fd -> file;
			if(cur_file == NULL || is_file_allow_write(cur_file)) {
				actual_byte_written = -1;
				break;
			}
			lock_acquire(&filesys_lock);
			actual_byte_written = (int) file_write(cur_file, buffer, length);
			lock_release(&filesys_lock);
			break;
	}
	return actual_byte_written;
};

static bool 
s_create(const char *file, unsigned initial_size){
	valid_get_addr(file);
	if(strlen(file) > 14){
		return false;
	}

	lock_acquire(&filesys_lock);
	bool result = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	if(!result){
		return false;
	}
	return true;
}

/* 파일 식별자로 변환하고 식별자 번호를 리턴한다. */
static int
s_open(const char *file){
	valid_get_addr(file);
	struct thread *cur = thread_current();

	char *fn_copy = palloc_get_page(0);
	if(fn_copy == NULL){
		return -1;
	}
	strlcpy(fn_copy, file, PGSIZE);

	
	lock_acquire(&filesys_lock);
	struct file *new_file = filesys_open(fn_copy);
	lock_release(&filesys_lock);


	palloc_free_page(fn_copy);
	if(new_file == NULL){
		return -1;
	}

	struct file_descriptor *wrap_fd = create_fd_wrapper(new_file, FD_FILE);
	if(wrap_fd == NULL){
		file_close(new_file);
		return -1;
	}

	int fd = -1;

	for (int i = 2; i < MAX_FD; i++)
	{
		if(cur -> fd_table[i] == NULL){
			cur -> fd_table[i] = wrap_fd;
			fd = i;
			break;
		}
	}

	if(fd < 0) {
		lock_acquire(&filesys_lock);
		file_close(new_file);
		lock_release(&filesys_lock);
		free(wrap_fd);
	}

	return fd;
}

static void 
s_close(int fd){
	struct thread *cur = thread_current();
	struct file_descriptor *target = get_fd_wrapper(fd);
	if(target != NULL){
		close_fd(target);
		cur -> fd_table[fd] = NULL;
	}
}

static int
s_filesize(int fd){
	struct file_descriptor *wrap_fd = get_fd_wrapper(fd);
	if(wrap_fd == NULL) return -1;
	struct file *cur_file = wrap_fd -> file;
	int file_len = -1;

	switch(wrap_fd -> type){
		case FD_STDIN:
			break;

		case FD_STDOUT:
			break;

		case FD_FILE:
			lock_acquire(&filesys_lock);
			file_len = (int) file_length(cur_file);
			lock_release(&filesys_lock);
			break;
	}
	return file_len;
}

static int 
s_read(int fd, void *buffer, unsigned size){

	if (size == 0)
		return 0;

	valid_put_buffer(buffer, size);

	if (!check_writable(buffer, size))
	 	s_exit(-1);

	struct file_descriptor *wrap_fd = get_fd_wrapper(fd);
	int bytes_rd = -1;

	if (wrap_fd == NULL){
		return -1;
	}

	switch (wrap_fd ->type){
		case FD_STDIN:
			unsigned rd_size = 0;
			uint8_t *buf = (uint8_t *) buffer;

			while(rd_size < size){
				uint8_t ch = input_getc();
				buf[rd_size] = ch;
				rd_size++;
			}
			bytes_rd = (int) rd_size;
			break;
		
		case FD_STDOUT:
			break;
		
		case FD_FILE:
			struct file *cur_file = wrap_fd -> file;
			if(cur_file == NULL){
				return -1;
			}
			lock_acquire(&filesys_lock);
			off_t bytes_read = file_read(cur_file, buffer, size);
			lock_release(&filesys_lock);
			bytes_rd = (int) bytes_read;
			break;

		default:
			break;
	}
	return bytes_rd;
}

static int 
s_exec(const char *cmd_line){
	valid_get_addr(cmd_line);
	char *cm_copy = palloc_get_page(PAL_ZERO);
	if(cm_copy == NULL) return -1;
	strlcpy(cm_copy, cmd_line, PGSIZE);
	return process_exec(cm_copy);
	
}

static int 
s_wait(tid_t pid){
	return process_wait(pid);
}

static tid_t 
s_fork(const char *thread_name, struct intr_frame *f){
	valid_get_addr(thread_name);
	return process_fork(thread_name, f);
}

static void 
s_seek(int fd, unsigned position){
	struct file_descriptor *wrap_fd = get_fd_wrapper(fd);
	if(wrap_fd == NULL || wrap_fd -> type == FD_STDIN || wrap_fd -> type == FD_STDOUT) return;
	lock_acquire(&filesys_lock);
	file_seek(wrap_fd -> file, position);
	lock_release(&filesys_lock);
}

static bool 
s_remove(const char *file){
	bool result = false;
	valid_get_addr(file);
	lock_acquire(&filesys_lock);
	result = filesys_remove(file);
	lock_release(&filesys_lock);
	return result;
}

static unsigned 
s_tell(int fd){
	struct file_descriptor *wrap_fd = get_fd_wrapper(fd);
	unsigned pos = 0;
	if(wrap_fd == NULL) return pos;
	lock_acquire(&filesys_lock);
	pos = file_tell(wrap_fd -> file);
	lock_release(&filesys_lock);
	return pos;
}


static int 
s_dup2(int oldfd, int newfd){
	if(oldfd < 0 || newfd < 0 || oldfd >= MAX_FD || newfd >= MAX_FD) return -1;
	struct file_descriptor *wrap_oldfd = get_fd_wrapper(oldfd);
	struct file_descriptor *wrap_newfd = get_fd_wrapper(newfd);
	struct thread *cur = thread_current();
	
	if(wrap_oldfd == NULL) return -1;
	if(wrap_newfd == wrap_oldfd) return newfd;
	if(wrap_newfd != NULL) close_fd(wrap_newfd);

	cur -> fd_table[newfd] = wrap_oldfd;
	wrap_oldfd -> ref_count++;
	return newfd;
}

/* file을 받으면 wrapper 구조체인 file_descriptor를 반환하는 함수 */
struct file_descriptor *create_fd_wrapper(struct file *f, enum fd_type f_type){
	//if(f == NULL) return NULL;
	struct file_descriptor *wrap_fd = (struct file_descriptor *)malloc(sizeof(struct file_descriptor));
	if(wrap_fd == NULL) return NULL;
	wrap_fd -> file = f;
	wrap_fd -> ref_count = 1;
	wrap_fd -> type = f_type;
	return wrap_fd;
}

void close_fd(struct file_descriptor *fd_wrapper){
	if(fd_wrapper == NULL) return;
	fd_wrapper -> ref_count--;

	if(fd_wrapper -> ref_count == 0){
		if(fd_wrapper -> type == FD_FILE){
			lock_acquire(&filesys_lock);
			file_close(fd_wrapper -> file);
			lock_release(&filesys_lock);
		}
		free(fd_wrapper);
	}
}

struct file_descriptor *get_fd_wrapper(int fd){
	if(fd < 0 || fd >= MAX_FD) return NULL;
	struct thread *cur = thread_current();
	struct file_descriptor *wrap_fd = cur -> fd_table[fd]; 
	return wrap_fd;
}



/* TODO : 혹시 시작 주소 다음 바이트에 문제가 생기면 사용하기 */
// static bool check_buffer(void *buffer, int length) {
//     for (int i = 0; i < length; i++) {
//         void *current_addr = ((char *)buffer) + i;
//         if (check_valid_access(current_addr) == false) {
//             return false;
//         }
//     }
//     return true;
// }