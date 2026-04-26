#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254] 문서를 참고한다. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 누적된 타이머 tick 수. */
static int64_t ticks;

static struct list sleep_list;

/* 타이머 tick 하나 동안 실행할 수 있는 busy-wait 반복 횟수.
   timer_calibrate()에서 초기화된다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 기능 : 8254 Programmable Interval Timer(PIT)가 TIMER_FREQ 횟수만큼
          초당 인터럽트를 발생시키도록 설정하고 인터럽트 핸들러를 등록한다.
   입출 : input : 없음
          output : 반환값은 없으며 PIT 설정과 외부 인터럽트 등록 상태를 변경한다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값을 반올림해 카운터 값으로 사용한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* 제어 워드: counter 0, LSB 후 MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 기능 : 짧은 지연을 구현하는 데 사용할 loops_per_tick 값을 보정한다.
   입출 : input : 없음
          output : 반환값은 없으며 전역 loops_per_tick 값을 설정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* timer tick 하나보다 짧게 끝나는 가장 큰 2의 거듭제곱 반복 횟수를 찾는다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 세밀하게 보정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* 기능 : OS 부팅 이후 지난 타이머 tick 수를 조회한다.
   입출 : input : 없음
          output : int64_t 타입의 현재 누적 tick 값을 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* 기능 : 특정 tick 시각 이후 경과한 tick 수를 계산한다.
   입출 : input : int64_t then, 이전에 timer_ticks()로 얻은 기준 tick 값
          output : int64_t 타입의 경과 tick 수를 반환한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 기능 : 현재 스레드 실행을 약 TICKS tick 동안 지연시킨다.
   입출 : input : int64_t ticks, 재울 tick 수
          output : 반환값은 없으며 현재 스레드가 CPU를 양보하거나 이후 구현에서 block 상태가 된다. */
void
timer_sleep (int64_t ticks) {
	// 실행 시점에서의 누적 tick 수
	int64_t start = timer_ticks ();
	// 현재 스레드를 저장 및 목표시간 저장
	struct thread *cur_thd = thread_current();
	// cur_thd->outTick = (start+ticks);

	// sleep_list 뒤에 넣고 thread block
	list_push_back(&sleep_list, &cur_thd->elem);
	// list_insert_ordered(&sleep_list, &cur_thd->elem, cur_thd->outTick, NULL);
	thread_block();
}

/* 기능 : 현재 스레드 실행을 약 MS 밀리초 동안 지연시킨다.
   입출 : input : int64_t ms, 지연할 밀리초 수
          output : 반환값은 없으며 real_time_sleep()을 통해 지연 동작을 수행한다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 기능 : 현재 스레드 실행을 약 US 마이크로초 동안 지연시킨다.
   입출 : input : int64_t us, 지연할 마이크로초 수
          output : 반환값은 없으며 real_time_sleep()을 통해 지연 동작을 수행한다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 기능 : 현재 스레드 실행을 약 NS 나노초 동안 지연시킨다.
   입출 : input : int64_t ns, 지연할 나노초 수
          output : 반환값은 없으며 real_time_sleep()을 통해 지연 동작을 수행한다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 기능 : 타이머 통계 정보를 출력한다.
   입출 : input : 없음
          output : 반환값은 없으며 현재 tick 수를 콘솔에 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 기능 : 타이머 인터럽트가 발생할 때마다 전역 tick을 증가시키고 스레드 tick 처리를 호출한다.
   입출 : input : struct intr_frame *args, 인터럽트 프레임 포인터이며 현재 구현에서는 사용하지 않는다
          output : 반환값은 없으며 ticks와 스케줄링 tick 상태를 변경한다. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;

	// 현재 스레드 -> sleep list -> outtick
	// 루프,  
	// while
	if(timer_ticks > &thd->outTick) {  // 반복 루프 종료 조건으로
	struct thread *thd = list_entry(list_pop_front(&sleep_list), struct thread, elem);
	
		thread_unblock(thd);
	}
}

/* 기능 : 지정한 반복 횟수가 timer tick 하나를 넘길 만큼 오래 걸리는지 검사한다.
   입출 : input : unsigned loops, busy_wait로 실행할 반복 횟수
          output : bool 타입으로 tick이 바뀌면 true, 아니면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* 다음 타이머 tick이 시작될 때까지 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* 지정한 횟수만큼 busy-wait 반복을 실행한다. */
	start = ticks;
	busy_wait (loops);

	/* tick 값이 바뀌었다면 반복 시간이 너무 길었다는 뜻이다. */
	barrier ();
	return start != ticks;
}

/* 기능 : 짧은 지연을 만들기 위해 단순 반복 루프를 LOOPS번 수행한다.
          코드 정렬이 시간 측정에 영향을 줄 수 있으므로 NO_INLINE으로 고정한다.
   입출 : input : int64_t loops, 반복할 횟수
          output : 반환값은 없으며 barrier()를 반복 실행해 시간을 소모한다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 기능 : NUM/DENOM 초에 해당하는 시간만큼 실행을 지연한다.
   입출 : input : int64_t num과 int32_t denom, 지연 시간을 분수 형태로 나타내는 값
          output : 반환값은 없으며 tick 단위 지연 또는 sub-tick busy-wait를 수행한다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM 초를 timer tick 수로 변환하고 소수점 이하는 버린다.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 한 tick 이상 기다려야 하므로 CPU를 양보할 수 있는 timer_sleep()을 사용한다. */
		timer_sleep (ticks);
	} else {
		/* 한 tick보다 짧은 지연은 더 정확한 시간을 위해 busy-wait를 사용한다.
		   오버플로 가능성을 낮추기 위해 분자와 분모를 1000 단위로 줄인다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
