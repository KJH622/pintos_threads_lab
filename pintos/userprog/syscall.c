#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void syscall_exit (int status);
static tid_t syscall_exec (const char *file);
static int syscall_wait (tid_t pid);

static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, const void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);

static tid_t syscall_fork (const char *thread_name, struct intr_frame *f);
static void syscall_halt (void);
static void syscall_invalid (void);

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
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	switch (f->R.rax) {
		case SYS_HALT:
			syscall_halt ();
			break;

		case SYS_EXIT:
			syscall_exit ((int) f->R.rdi);
			break;

		case SYS_FORK:
			f->R.rax = syscall_fork ((const char *) f->R.rdi, f);
			break;

		case SYS_EXEC:
			f->R.rax = syscall_exec ((const char *) f->R.rdi);
			break;

		case SYS_WAIT:
			f->R.rax = syscall_wait ((tid_t) f->R.rdi);
			break;

		case SYS_CREATE:
			f->R.rax = syscall_create ((const char *) f->R.rdi,
									   (unsigned) f->R.rsi);
			break;

		case SYS_REMOVE:
			f->R.rax = syscall_remove ((const char *) f->R.rdi);
			break;

		case SYS_OPEN:
			f->R.rax = syscall_open ((const char *) f->R.rdi);
			break;

		case SYS_FILESIZE:
			f->R.rax = syscall_filesize ((int) f->R.rdi);
			break;

		case SYS_READ:
			f->R.rax = syscall_read ((int) f->R.rdi,
									 (void *) f->R.rsi,
									 (unsigned) f->R.rdx);
			break;

		case SYS_WRITE:
			f->R.rax = syscall_write ((int) f->R.rdi,
									  (const void *) f->R.rsi,
									  (unsigned) f->R.rdx);
			break;

		case SYS_SEEK:
			syscall_seek ((int) f->R.rdi, (unsigned) f->R.rsi);
			break;

		case SYS_TELL:
			f->R.rax = syscall_tell ((int) f->R.rdi);
			break;

		case SYS_CLOSE:
			syscall_close ((int) f->R.rdi);
			break;

		default:
			syscall_invalid ();
			break;
	}
}

/* fd_table에서 fd에 대응하는 file 포인터를 반환한다.
   fd가 범위를 벗어나거나 할당되지 않은 경우 NULL을 반환한다. */
static struct file *
fd_to_file(int fd) {
    return thread_current()->fd_table[fd];
}

static int
fd_alloc(struct file *f) {
    return -1;
}

static void
fd_free(int fd) {}

static void
syscall_exit (int status) {
	printf ("%s: exit(%d)\n", thread_current ()->name, status);
	thread_exit ();
}

static tid_t
syscall_exec (const char *file UNUSED) {
	return -1;
}

static int
syscall_wait (tid_t pid) {
	return process_wait (pid);
}

static bool
syscall_create (const char *file UNUSED, unsigned initial_size UNUSED) {
	return false;
}

static bool
syscall_remove (const char *file UNUSED) {
	return false;
}

static int
syscall_open (const char *file UNUSED) {
	return -1;
}

static int
syscall_filesize (int fd UNUSED) {
	return -1;
}

static int
syscall_read (int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED) {
	return -1;
}

static int
syscall_write (int fd, const void *buffer, unsigned size) {
	if (fd == 1) {
		putbuf (buffer, size);
		return size;
	}
	return -1;
}

static void
syscall_seek (int fd UNUSED, unsigned position UNUSED) {
}

static unsigned
syscall_tell (int fd UNUSED) {
	return 0;
}

static void
syscall_close (int fd UNUSED) {
}

static tid_t
syscall_fork (const char *thread_name UNUSED, struct intr_frame *f UNUSED) {
	return -1;
}

static void
syscall_halt (void) {
	power_off ();
}

static void
syscall_invalid (void) {
	syscall_exit (-1);
}
