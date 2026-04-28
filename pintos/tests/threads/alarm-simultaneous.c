/* 같은 sleep 시간을 가진 여러 스레드를 만들고 동시에 깨어나는지 기록한다.
   같은 iteration 안에서는 모든 스레드가 같은 tick에 깨어나야 한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

/* 기능 : 3개 스레드가 같은 tick에 5번 반복해서 동시에 깨어나는지 테스트한다.
   입출 : input : 없음
          output : 반환값은 없으며 기대 출력과 다르면 테스트가 실패한다. */
void
test_alarm_simultaneous (void) 
{
  test_sleep (3, 5);
}

/* 테스트 전체가 공유하는 정보. */
struct sleep_test 
  {
    int64_t start;              /* 테스트 기준 시작 tick. */
    int iterations;             /* 각 스레드가 sleep을 반복할 횟수. */
    int *output_pos;            /* 출력 버퍼의 현재 기록 위치. */
  };

static void sleeper (void *);

/* 기능 : THREAD_CNT개의 스레드를 만들고 각 스레드가 ITERATIONS번 같은 시각에 깨어나도록 실행한다.
   입출 : input : int thread_cnt는 생성할 스레드 수, int iterations는 각 스레드의 반복 횟수
          output : 반환값은 없으며 wakeup tick 간격을 출력 버퍼에 기록하고 메시지로 출력한다. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  int *output;
  int i;

  /* 이 테스트는 MLFQS 모드에서 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  msg ("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
  msg ("Each thread sleeps 10 ticks each time.");
  msg ("Within an iteration, all threads should wake up on the same tick.");

  /* wakeup tick 기록용 출력 버퍼를 할당한다. */
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* 테스트 기준 시각과 반복 횟수를 초기화한다. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  test.output_pos = output;

  /* sleeper 스레드들을 생성한다. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      char name[16];
      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, &test);
    }
  
  /* 모든 sleeper 스레드가 끝날 만큼 충분히 기다린다. */
  timer_sleep (100 + iterations * 10 + 100);

  /* 각 wakeup 사이의 tick 차이를 출력한다. */
  msg ("iteration 0, thread 0: woke up after %d ticks", output[0]);
  for (i = 1; i < test.output_pos - output; i++) 
    msg ("iteration %d, thread %d: woke up %d ticks later",
         i / thread_cnt, i % thread_cnt, output[i] - output[i - 1]);
  
  free (output);
}

/* 기능 : 매 iteration마다 같은 목표 tick까지 sleep한 뒤 실제 깨어난 tick을 기록한다.
   입출 : input : void *test_, struct sleep_test 포인터로 변환되는 공유 테스트 정보
          output : 반환값은 없으며 output_pos가 가리키는 버퍼에 경과 tick을 기록한다. */
static void
sleeper (void *test_) 
{
  struct sleep_test *test = test_;
  int i;

  /* timer tick의 시작 지점에 맞춰 race 가능성을 줄인다. */
  timer_sleep (1);

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * 10;
      timer_sleep (sleep_until - timer_ticks ());
      *test->output_pos++ = timer_ticks () - test->start;
      thread_yield ();
    }
}
