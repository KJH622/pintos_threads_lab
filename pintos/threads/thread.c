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

/* struct thread의 `magic` 멤버에 사용할 임의의 값.
   스택 오버플로를 감지하는 데 사용한다. 자세한 내용은
   thread.h 맨 위의 긴 주석을 참고하라. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드를 위한 임의의 값.
   이 값은 수정하지 말 것. */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태의 프로세스 목록. 즉, 실행할 준비는 되었지만
   실제로 실행 중은 아닌 프로세스들이다. */
static struct list ready_list;

/* timer_sleep()으로 잠든 스레드들을 깨울 tick 순서대로 보관하는 리스트. */
static struct list sleep_list;

/* 유휴 스레드. */
static struct thread *idle_thread;

/* 초기 스레드. init.c:main()을 실행하는 스레드이다. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 락. */
static struct lock tid_lock;

/* 스레드 제거 요청 목록. */
static struct list destruction_req;

/* 통계. */
static long long idle_ticks;    /* 유휴 상태로 보낸 타이머 틱 수. */
static long long kernel_ticks;  /* 커널 스레드에서 보낸 타이머 틱 수. */
static long long user_ticks;    /* 사용자 프로그램에서 보낸 타이머 틱 수. */

/* 스케줄링. */
#define TIME_SLICE 4            /* 각 스레드에 부여할 타이머 틱 수. */
static unsigned thread_ticks;   /* 마지막 양보 이후 지난 타이머 틱 수. */

/* false(기본값)이면 라운드 로빈 스케줄러를 사용한다.
   true이면 다단계 피드백 큐 스케줄러를 사용한다.
   커널 명령줄 옵션 "-o mlfqs"로 제어한다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드를 가리키는 것처럼 보이면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 스레드를 반환한다.
 * CPU의 스택 포인터 `rsp`를 읽은 뒤 페이지 시작 주소로
 * 내림 정렬한다. `struct thread`는 항상 페이지의 시작에 있고
 * 스택 포인터는 그 중간 어딘가에 있으므로, 이를 통해 현재
 * 스레드를 찾을 수 있다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start를 위한 전역 디스크립터 테이블.
// gdt는 thread_init 이후에 설정되므로, 임시 gdt를 먼저 설정해야 한다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화한다.
   일반적으로는 불가능하지만, 이 경우에는 loader.S가 스택의 바닥을
   페이지 경계에 조심스럽게 배치했기 때문에 가능하다.

   실행 큐와 tid 락도 함께 초기화한다.

   이 함수를 호출한 뒤 thread_create()로 스레드를 만들기 전에
   반드시 페이지 할당자를 초기화해야 한다.

   이 함수가 끝나기 전에는 thread_current()를 호출해도 안전하지 않다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 커널을 위한 임시 gdt를 다시 로드한다.
	 * 이 gdt에는 사용자 컨텍스트가 포함되어 있지 않다.
	 * 커널은 gdt_init()에서 사용자 컨텍스트를 포함해 gdt를 다시 구성한다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 전역 스레드 컨텍스트를 초기화한다. */
	lock_init (&tid_lock);
	list_init (&ready_list);
    list_init (&sleep_list);
	list_init (&destruction_req);

	/* 실행 중인 스레드를 위한 스레드 구조체를 설정한다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작한다.
   유휴 스레드도 함께 생성한다. */
void
thread_start (void) {
	/* 유휴 스레드를 생성한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
    // idle 쓰레드 : CPU가 할 일이 없을 때 대신 실행되는 빈 스레드

	/* 선점형 스레드 스케줄링을 시작한다. */
	intr_enable ();

	/* 유휴 스레드가 idle_thread를 초기화할 때까지 기다린다. */
	sema_down (&idle_started);
}

/* 매 타이머 틱마다 타이머 인터럽트 핸들러가 호출한다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행된다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계를 갱신한다. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점을 강제한다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 스레드 통계를 출력한다. */
// 부팅부터 종료까지 전체 실행 기간의 CPU 사용 요약
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 주어진 초기 PRIORITY를 갖고, AUX를 인자로 전달받아 FUNCTION을 실행하는
   NAME이라는 새 커널 스레드를 만든 뒤 준비 큐에 추가한다. 새 스레드의
   스레드 식별자를 반환하며, 생성에 실패하면 TID_ERROR를 반환한다.

   thread_start()가 호출된 뒤라면 새 스레드는 thread_create()가 반환되기
   전에 스케줄될 수 있다. 심지어 thread_create()가 반환되기 전에 종료될
   수도 있다. 반대로, 새 스레드가 스케줄되기 전까지 원래 스레드가 얼마든지
   오래 실행될 수도 있다. 순서를 보장해야 한다면 세마포어나 다른 형태의
   동기화를 사용하라.

   제공된 코드는 새 스레드의 `priority` 멤버를 PRIORITY로 설정하지만,
   실제 우선순위 스케줄링은 구현되어 있지 않다. 우선순위 스케줄링은
   Problem 1-3의 목표이다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 스레드를 할당한다. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드를 초기화한다. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 스케줄되면 kernel_thread를 호출한다.
	 * 참고: rdi는 첫 번째 인자이고, rsi는 두 번째 인자이다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 큐에 추가한다. */
	thread_unblock (t);

	return tid;
}

void
thread_sleep (struct thread *wakeup_tick) {
    enum intr_level old_level;
    
    old_level = intr_disable ();
    list_push_back (&sleep_list, &wakeup_tick->elem);
    list_insert_ordered (&sleep_list, &wakeup_tick->elem, wakeup_tick, NULL);
    thread_block ();
    intr_set_level(old_level);


}

/* 현재 스레드를 잠들게 한다. thread_unblock()으로 깨워지기 전까지는
   다시 스케줄되지 않는다.

   이 함수는 인터럽트가 꺼진 상태에서 호출되어야 한다. 일반적으로는
   synch.h의 동기화 기본 요소 중 하나를 사용하는 편이 더 좋다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* 블록된 스레드 T를 실행 준비 상태로 전환한다. T가 블록된 상태가 아니라면
   오류이다. 실행 중인 스레드를 준비 상태로 만들려면 thread_yield()를
   사용하라.

   이 함수는 실행 중인 스레드를 선점하지 않는다. 이는 중요할 수 있다.
   호출자가 직접 인터럽트를 비활성화했다면, 스레드를 깨우고 다른 데이터를
   갱신하는 작업이 원자적으로 이루어지기를 기대할 수 있기 때문이다. */
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

/* 실행 중인 스레드의 이름을 반환한다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 실행 중인 스레드를 반환한다.
   running_thread()에 몇 가지 안전성 검사를 더한 것이다.
   자세한 내용은 thread.h 맨 위의 긴 주석을 참고하라. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* T가 실제로 스레드인지 확인한다.
	   이 assertion 중 하나라도 실패하면 스레드의 스택이 오버플로되었을
	   수 있다. 각 스레드의 스택은 4 kB보다 작으므로, 몇 개의 큰 자동
	   배열이나 적당한 수준의 재귀만으로도 스택 오버플로가 발생할 수 있다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 실행 중인 스레드의 tid를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드를 스케줄 대상에서 제거하고 파괴한다.
   호출자에게 절대 반환하지 않는다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* 현재 상태를 dying으로 설정하고 다른 프로세스를 스케줄한다.
	   schedule_tail() 호출 중에 파괴될 것이다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* CPU를 양보한다. 현재 스레드는 잠들지 않으며, 스케줄러의 판단에 따라
   즉시 다시 스케줄될 수도 있다. */
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

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정한다. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* 현재 스레드의 nice 값을 NICE로 설정한다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: 여기에 구현을 작성한다. */
}

/* 현재 스레드의 nice 값을 반환한다. */
int
thread_get_nice (void) {
	/* TODO: 여기에 구현을 작성한다. */
	return 0;
}

/* 시스템 load average에 100을 곱한 값을 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: 여기에 구현을 작성한다. */
	return 0;
}

/* 현재 스레드의 recent_cpu 값에 100을 곱한 값을 반환한다. */
int
thread_get_recent_cpu (void) {
	/* TODO: 여기에 구현을 작성한다. */
	return 0;
}

/* 유휴 스레드. 실행할 준비가 된 다른 스레드가 없을 때 실행된다.

   유휴 스레드는 처음에 thread_start()에 의해 준비 리스트에 들어간다.
   최초에 한 번 스케줄되면 idle_thread를 초기화하고, thread_start()가
   계속 진행할 수 있도록 전달받은 세마포어를 "up"한 뒤 즉시 블록된다.
   그 이후 유휴 스레드는 준비 리스트에 나타나지 않는다. 준비 리스트가
   비어 있을 때 next_thread_to_run()이 특수한 경우로 이 스레드를 반환한다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* 다른 스레드가 실행되도록 한다. */
		intr_disable ();
		thread_block ();

		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다린다.

		   `sti` 명령은 다음 명령이 완료될 때까지 인터럽트를 비활성화하므로,
		   이 두 명령은 원자적으로 실행된다. 이 원자성은 중요하다. 그렇지
		   않으면 인터럽트를 다시 활성화한 뒤 다음 인터럽트를 기다리기 전에
		   인터럽트가 처리될 수 있고, 최대 한 클록 틱만큼의 시간이 낭비될
		   수 있다.

		   [IA32-v2a] "HLT", [IA32-v2b] "STI", [IA32-v3a]
		   7.11.1 "HLT Instruction"를 참고하라. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 기반으로 사용되는 함수. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* 스케줄러는 인터럽트가 꺼진 상태로 실행된다. */
	function (aux);       /* 스레드 함수를 실행한다. */
	thread_exit ();       /* function()이 반환되면 스레드를 종료한다. */
}


/* T를 NAME이라는 이름의 블록된 스레드로 기본 초기화한다. */
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

/* 다음에 스케줄할 스레드를 선택해 반환한다. 실행 큐가 비어 있지 않다면
   실행 큐에서 스레드를 반환해야 한다. 실행 중인 스레드가 계속 실행될 수
   있다면 그 스레드는 실행 큐 안에 있을 것이다. 실행 큐가 비어 있으면
   idle_thread를 반환한다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* iretq를 사용해 스레드를 시작한다. */
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

/* 새 스레드의 페이지 테이블을 활성화하고, 이전 스레드가 종료 중이라면
   그것을 파괴하여 스레드를 전환한다.

   이 함수가 호출되는 시점에는 방금 PREV 스레드에서 전환된 상태이며,
   새 스레드는 이미 실행 중이고 인터럽트는 여전히 비활성화되어 있다.

   스레드 전환이 완료되기 전에는 printf()를 호출해도 안전하지 않다.
   실제로는 printf()를 함수 끝에 추가해야 한다는 뜻이다. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* 주된 전환 로직.
	 * 먼저 전체 실행 컨텍스트를 intr_frame에 복원한 다음,
	 * do_iret를 호출해 다음 스레드로 전환한다.
	 * 여기서부터 전환이 끝날 때까지는 어떤 스택도 사용하면 안 된다. */
	__asm __volatile (
			/* 사용할 레지스터를 저장한다. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 입력값을 한 번만 가져온다. */
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
			"pop %%rbx\n"              // 저장한 rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // 저장한 rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // 저장한 rax
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

/* 새 프로세스를 스케줄한다. 진입 시점에 인터럽트는 꺼져 있어야 한다.
 * 이 함수는 현재 스레드의 상태를 status로 바꾼 뒤,
 * 실행할 다른 스레드를 찾아 그 스레드로 전환한다.
 * schedule() 안에서 printf()를 호출하는 것은 안전하지 않다. */
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

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* 다음 스레드를 실행 중으로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새 time slice를 시작한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새 주소 공간을 활성화한다. */
	process_activate (next);
#endif

	if (curr != next) {
		/* 전환해 나온 스레드가 종료 중이라면 그 struct thread를 파괴한다.
		   thread_exit()가 자기 자신의 기반을 무너뜨리지 않도록 이 작업은
		   늦게 이루어져야 한다.
		   현재 페이지가 스택으로 사용되고 있으므로 여기서는 페이지 해제
		   요청만 큐에 넣는다.
		   실제 파괴 로직은 schedule()의 시작 부분에서 호출된다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* 스레드를 전환하기 전에 현재 실행 중인 스레드의 정보를 먼저 저장한다. */
		thread_launch (next);
	}
}

/* 새 스레드에 사용할 tid를 반환한다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
