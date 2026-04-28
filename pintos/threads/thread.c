#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 `magic' 멤버에 넣는 임의 값.
   스택 오버플로를 감지하는 데 사용한다. 자세한 내용은 thread.h 상단 주석을 참고한다. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 thread 검사용 임의 값.
   이 값은 수정하지 않는다. */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태의 스레드 목록.
   실행 준비는 되었지만 현재 CPU에서 실행 중은 아닌 스레드들이 들어간다. */
static struct list ready_list;

/* 실행 가능한 스레드가 없을 때 실행되는 idle 스레드. */
static struct thread *idle_thread;

/* init.c:main()을 실행하는 초기 스레드. */
static struct thread *initial_thread;

/* allocate_tid()에서 tid 할당을 보호하는 lock. */
static struct lock tid_lock;

/* 제거해야 할 스레드 페이지 요청 목록. */
static struct list destruction_req;

/* 통계 값. */
static long long idle_ticks;    /* idle 상태로 보낸 timer tick 수. */
static long long kernel_ticks;  /* kernel thread에서 보낸 timer tick 수. */
static long long user_ticks;    /* user program에서 보낸 timer tick 수. */

/* 스케줄링 설정. */
#define TIME_SLICE 4            /* 각 스레드에 부여되는 timer tick 수. */
static unsigned thread_ticks;   /* 마지막 yield 이후 현재 스레드가 사용한 tick 수. */

/* false이면 기본 round-robin 스케줄러를 사용한다.
   true이면 multi-level feedback queue scheduler를 사용한다.
   커널 명령행 옵션 "-o mlfqs"로 제어된다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 thread를 가리키는 것처럼 보이면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 thread를 반환한다.
 * CPU의 stack pointer `rsp'를 읽은 뒤 페이지 시작 주소로 내림한다.
 * `struct thread'는 항상 페이지 시작에 있고 stack pointer는 같은 페이지 중간 어딘가에
 * 있으므로 이 계산으로 현재 thread를 찾을 수 있다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start를 위한 Global Descriptor Table.
// 실제 gdt는 thread_init 이후 설정되므로 먼저 임시 gdt를 준비한다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 기능 : 현재 실행 중인 코드를 초기 thread로 등록해 스레드 시스템을 초기화한다.
          loader.S가 스택 하단을 페이지 경계에 맞춰 두었기 때문에 가능하다.
          ready_list, tid_lock, destruction_req도 함께 초기화한다.
          이 함수가 끝나기 전에는 thread_current()를 안전하게 호출할 수 없다.
   입출 : input : 없음
          output : 반환값은 없으며 initial_thread와 전역 스레드 관리 상태를 초기화한다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 커널용 임시 gdt를 다시 적재한다.
	 * 이 gdt에는 user context가 포함되지 않는다.
	 * 커널은 이후 gdt_init()에서 user context를 포함해 gdt를 다시 구성한다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 전역 thread context를 초기화한다. */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* 현재 실행 중인 흐름을 위한 thread 구조체를 설정한다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 기능 : idle thread를 생성하고 interrupt를 켜서 선점형 스케줄링을 시작한다.
   입출 : input : 없음
          output : 반환값은 없으며 idle thread가 초기화될 때까지 기다린다. */
void
thread_start (void) {
	/* idle thread를 생성한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 선점형 스케줄링을 시작한다. */
	intr_enable ();

	/* idle thread가 idle_thread 전역 포인터를 초기화할 때까지 기다린다. */
	sema_down (&idle_started);
}

/* 기능 : 매 timer tick마다 호출되어 현재 스레드의 통계와 time slice를 갱신한다.
          외부 interrupt context에서 실행된다.
   입출 : input : 없음
          output : 반환값은 없으며 tick 통계를 갱신하고 필요하면 interrupt 반환 시 yield를 예약한다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 실행 중인 thread 종류에 따라 tick 통계를 갱신한다. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* time slice가 끝나면 선점을 예약한다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 기능 : 스레드 실행 통계를 출력한다.
   입출 : input : 없음
          output : 반환값은 없으며 idle/kernel/user tick 수를 콘솔에 출력한다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 기능 : NAME 이름과 PRIORITY 초기 우선순위를 가진 새 커널 thread를 생성한다.
          새 thread는 FUNCTION(AUX)를 실행하며 ready queue에 추가된다.
          thread_start() 이후라면 thread_create()가 반환되기 전에 새 thread가 실행되거나
          종료될 수도 있으므로 순서 보장이 필요하면 semaphore 같은 동기화 도구를 사용해야 한다.
   입출 : input : const char *name, int priority, thread_func *function, void *aux
          output : 성공 시 새 thread의 tid_t를 반환하고, 할당 실패 시 TID_ERROR를 반환한다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* thread 페이지를 할당한다. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* thread 구조체를 초기화하고 tid를 할당한다. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 스케줄되면 kernel_thread를 호출하도록 초기 interrupt frame을 구성한다.
	 * 참고: rdi는 첫 번째 인자, rsi는 두 번째 인자다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* ready queue에 추가한다. */
	thread_unblock (t);

	return tid;
}

/* 기능 : 현재 thread를 block 상태로 바꿔 재운다.
          thread_unblock()으로 깨워질 때까지 다시 스케줄되지 않는다.
          interrupt가 꺼진 상태에서 호출해야 하며, 일반적으로는 synch.h의 동기화 primitive를 쓰는 편이 낫다.
   입출 : input : 없음
          output : 반환값은 없으며 현재 thread 상태를 THREAD_BLOCKED로 바꾸고 schedule()을 호출한다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* 기능 : blocked 상태의 thread T를 ready-to-run 상태로 전환한다.
          T가 blocked 상태가 아니면 오류다. 실행 중인 thread를 ready로 만들려면 thread_yield()를 사용한다.
          이 함수는 현재 실행 중인 thread를 즉시 선점하지 않는다.
   입출 : input : struct thread *t
          output : 반환값은 없으며 t를 ready_list에 추가하고 상태를 THREAD_READY로 바꾼다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* 기능 : 현재 실행 중인 thread의 이름을 반환한다.
   입출 : input : 없음
          output : const char 포인터 형태의 thread 이름을 반환한다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 기능 : 현재 실행 중인 thread 구조체를 반환한다.
          running_thread() 결과에 유효성 검사를 추가로 수행한다.
   입출 : input : 없음
          output : struct thread 포인터를 반환한다. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* T가 실제 thread인지 확인한다.
	   assertion이 발생하면 스택 오버플로 가능성이 있다. 각 thread의 스택은 4 kB보다 작으므로
	   큰 자동 배열이나 깊은 재귀가 스택 오버플로를 만들 수 있다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 기능 : 현재 실행 중인 thread의 tid를 반환한다.
   입출 : input : 없음
          output : tid_t 타입의 현재 thread ID를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* 기능 : 현재 thread를 스케줄 대상에서 제거하고 종료 처리한다.
          호출자에게 반환하지 않는다.
   입출 : input : 없음
          output : 반환하지 않으며 현재 thread를 THREAD_DYING 상태로 바꾸고 다른 thread로 전환한다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* 현재 상태를 dying으로 바꾸고 다른 process/thread를 스케줄한다.
	   실제 페이지 해제는 이후 스케줄 과정에서 처리된다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* 기능 : 현재 thread가 CPU를 양보한다.
          sleep 상태가 되는 것은 아니므로 scheduler 선택에 따라 바로 다시 실행될 수 있다.
   입출 : input : 없음
          output : 반환값은 없으며 현재 thread를 ready_list에 넣고 schedule()을 수행한다. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 기능 : 현재 thread의 priority를 NEW_PRIORITY로 설정한다.
   입출 : input : int new_priority
          output : 반환값은 없으며 현재 thread의 priority 필드를 변경한다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* 기능 : 현재 thread의 priority를 반환한다.
   입출 : input : 없음
          output : int 타입의 현재 priority 값을 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* 기능 : 현재 thread의 nice 값을 NICE로 설정한다.
   입출 : input : int nice, MLFQS 계산에 사용할 nice 값
          output : 반환값은 없으며 현재 기본 구현에서는 아직 값을 저장하지 않는다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: 이곳에 구현을 추가한다. */
}

/* 기능 : 현재 thread의 nice 값을 반환한다.
   입출 : input : 없음
          output : int 타입의 nice 값을 반환하며 현재 기본 구현은 0을 반환한다. */
int
thread_get_nice (void) {
	/* TODO: 이곳에 구현을 추가한다. */
	return 0;
}

/* 기능 : 시스템 load average 값을 100배 스케일로 반환한다.
   입출 : input : 없음
          output : int 타입의 load_avg * 100 값을 반환하며 현재 기본 구현은 0을 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: 이곳에 구현을 추가한다. */
	return 0;
}

/* 기능 : 현재 thread의 recent_cpu 값을 100배 스케일로 반환한다.
   입출 : input : 없음
          output : int 타입의 recent_cpu * 100 값을 반환하며 현재 기본 구현은 0을 반환한다. */
int
thread_get_recent_cpu (void) {
	/* TODO: 이곳에 구현을 추가한다. */
	return 0;
}

/* 기능 : 실행 가능한 다른 thread가 없을 때 동작하는 idle thread 함수다.
          thread_start()에서 처음 ready list에 들어가며, 최초 실행 시 idle_thread를 초기화하고
          전달받은 semaphore를 up해서 thread_start()가 계속 진행되게 한 뒤 즉시 block된다.
          이후 ready_list에는 들어가지 않고, ready_list가 비었을 때 next_thread_to_run()에서 특수하게 선택된다.
   입출 : input : void *idle_started_, struct semaphore 포인터로 변환되는 시작 동기화 객체
          output : 반환하지 않으며 무한 루프에서 interrupt를 기다린다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* 다른 thread가 실행될 수 있게 CPU를 양보한다. */
		intr_disable ();
		thread_block ();

		/* interrupt를 다시 켜고 다음 interrupt를 기다린다.
		   `sti' 명령은 다음 명령이 끝날 때까지 interrupt를 비활성 상태로 유지하므로
		   `sti; hlt'는 원자적으로 실행된다. 이 원자성이 없으면 interrupt 재활성화와
		   대기 사이에 interrupt가 처리되어 최대 한 clock tick을 낭비할 수 있다.
		   자세한 내용은 [IA32-v2a] "HLT", [IA32-v2b] "STI",
		   [IA32-v3a] 7.11.1 "HLT Instruction"을 참고한다. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 기능 : 새 kernel thread가 실제 thread 함수 FUNCTION(AUX)를 실행하도록 감싸는 진입 함수다.
   입출 : input : thread_func *function, void *aux
          output : 반환값은 없으며 function이 반환하면 thread_exit()로 현재 thread를 종료한다. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* scheduler는 interrupt가 꺼진 상태에서 실행되므로 여기서 다시 켠다. */
	function (aux);       /* thread 본문 함수를 실행한다. */
	thread_exit ();       /* function()이 반환하면 thread를 종료한다. */
}


/* 기능 : T를 NAME 이름과 PRIORITY 우선순위를 가진 blocked thread로 기본 초기화한다.
   입출 : input : struct thread *t, const char *name, int priority
          output : 반환값은 없으며 t의 상태, 이름, 스택 포인터, priority, magic 값을 설정한다. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* 기능 : 다음에 스케줄할 thread를 선택해 반환한다.
          ready_list가 비어 있으면 idle_thread를 반환하고, 아니면 ready_list 앞의 thread를 꺼낸다.
   입출 : input : 없음
          output : struct thread 포인터를 반환한다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* 기능 : iretq를 사용해 저장된 interrupt frame의 실행 문맥으로 진입한다.
   입출 : input : struct intr_frame *tf
          output : 일반적인 방식으로 반환하지 않고 tf에 저장된 레지스터와 명령 위치로 전환한다. */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* 기능 : 새 thread의 실행 문맥으로 전환한다.
          이전 thread가 dying 상태라면 이후 안전한 시점에 제거되도록 처리한다.
          thread 전환이 완료되기 전에는 printf()를 호출하면 안전하지 않다.
   입출 : input : struct thread *th, 다음에 실행할 thread
          output : 반환값은 없으며 CPU 실행 문맥을 th로 전환한다. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* 핵심 thread 전환 로직.
	 * 먼저 현재 실행 문맥 전체를 intr_frame에 저장한 뒤 do_iret을 호출해 다음 thread로 전환한다.
	 * 이 지점부터 전환이 끝날 때까지는 stack 사용을 피해야 한다. */
	__asm __volatile (
			/* 사용할 레지스터를 저장한다. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 입력 값을 한 번만 읽는다. */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // 저장된 rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // 저장된 rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // 저장된 rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // 현재 rip를 읽는다.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* 기능 : 현재 thread 상태를 status로 바꾼 뒤 다음 thread를 찾아 전환한다.
          진입 시 interrupt는 반드시 꺼져 있어야 하며 schedule() 중 printf() 호출은 안전하지 않다.
   입출 : input : int status, 현재 thread에 설정할 thread_status 값
          output : 반환값은 없으며 현재 thread 상태와 CPU 실행 문맥을 변경한다. */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

/* 기능 : ready_list에서 다음 thread를 선택해 실행 상태로 만들고 필요하면 문맥 전환을 수행한다.
   입출 : input : 없음
          output : 반환값은 없으며 curr에서 next로 실행 문맥이 전환될 수 있다. */
static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* 선택된 thread를 running 상태로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새 time slice를 시작한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새 주소 공간을 활성화한다. */
	process_activate (next);
#endif

	if (curr != next) {
		/* 전환 전 thread가 dying 상태라면 struct thread를 제거 대상으로 등록한다.
		   thread_exit() 실행 중 자기 stack 페이지를 즉시 해제하면 안 되므로 여기서는
		   페이지 해제 요청만 큐에 넣고, 실제 해제는 다음 schedule() 시작 시 처리한다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* thread를 전환하기 전에 현재 실행 정보를 먼저 저장한다. */
		thread_launch (next);
	}
}

/* 기능 : 새 thread에 사용할 tid를 할당한다.
   입출 : input : 없음
          output : tid_t 타입의 새 thread ID를 반환하며 내부 next_tid 값을 증가시킨다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
