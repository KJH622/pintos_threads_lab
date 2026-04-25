/* 서로 다른 고정 sleep 시간을 가진 N개 스레드를 만들고 각 스레드를 M번 재운다.
   깨어난 순서를 기록한 뒤 시간 순서가 올바른지 검증한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

/* 기능 : 5개 스레드가 각자 한 번씩 sleep하는 alarm 기본 동작을 테스트한다.
   입출 : input : 없음
          output : 반환값은 없으며 실패 시 fail()/PANIC으로 테스트를 중단한다. */
void
test_alarm_single (void) 
{
  test_sleep (5, 1);
}

/* 기능 : 5개 스레드가 여러 번 반복 sleep해도 wakeup 순서가 유지되는지 테스트한다.
   입출 : input : 없음
          output : 반환값은 없으며 실패 시 fail()/PANIC으로 테스트를 중단한다. */
void
test_alarm_multiple (void) 
{
  test_sleep (5, 7);
}

/* 테스트 전체가 공유하는 정보. */
struct sleep_test 
  {
    int64_t start;              /* 테스트 기준 시작 tick. */
    int iterations;             /* 각 스레드가 sleep을 반복할 횟수. */

    /* 출력 기록용 정보. */
    struct lock output_lock;    /* 출력 버퍼를 보호하는 lock. */
    int *output_pos;            /* 출력 버퍼의 현재 기록 위치. */
  };

/* 테스트에 참여하는 개별 스레드의 정보. */
struct sleep_thread 
  {
    struct sleep_test *test;     /* 모든 스레드가 공유하는 테스트 정보. */
    int id;                     /* sleeper 식별자. */
    int duration;               /* 한 번에 sleep할 tick 수. */
    int iterations;             /* 지금까지 깨어난 반복 횟수. */
  };

static void sleeper (void *);

/* 기능 : THREAD_CNT개의 sleeper 스레드를 만들고 각 스레드가 ITERATIONS번 sleep하도록 실행한다.
   입출 : input : int thread_cnt는 생성할 스레드 수, int iterations는 각 스레드의 반복 횟수
          output : 반환값은 없으며 wakeup 순서가 잘못되면 fail()로 테스트를 실패 처리한다. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  struct sleep_thread *threads;
  int *output, *op;
  int product;
  int i;

  /* 이 테스트는 MLFQS 모드에서 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  msg ("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
  msg ("Thread 0 sleeps 10 ticks each time,");
  msg ("thread 1 sleeps 20 ticks each time, and so on.");
  msg ("If successful, product of iteration count and");
  msg ("sleep duration will appear in nondescending order.");

  /* 테스트 정보와 출력 버퍼에 필요한 메모리를 할당한다. */
  threads = malloc (sizeof *threads * thread_cnt);
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (threads == NULL || output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* 테스트 기준 시각과 출력 보호용 lock을 초기화한다. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  lock_init (&test.output_lock);
  test.output_pos = output;

  /* sleeper 스레드들을 생성한다. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      struct sleep_thread *t = threads + i;
      char name[16];
      
      t->test = &test;
      t->id = i;
      t->duration = (i + 1) * 10;
      t->iterations = 0;

      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, t);
    }
  
  /* 모든 스레드가 끝날 만큼 충분히 기다린다. */
  timer_sleep (100 + thread_cnt * iterations * 10 + 100);

  /* 예상 밖으로 아직 실행 중인 스레드가 있을 수 있으므로 출력 lock을 잡는다. */
  lock_acquire (&test.output_lock);

  /* 완료 순서를 출력하고 product 값이 내림차순으로 깨지지 않는지 검사한다. */
  product = 0;
  for (op = output; op < test.output_pos; op++) 
    {
      struct sleep_thread *t;
      int new_prod;

      ASSERT (*op >= 0 && *op < thread_cnt);
      t = threads + *op;

      new_prod = ++t->iterations * t->duration;
        
      msg ("thread %d: duration=%d, iteration=%d, product=%d",
           t->id, t->duration, t->iterations, new_prod);
      
      if (new_prod >= product)
        product = new_prod;
      else
        fail ("thread %d woke up out of order (%d > %d)!",
              t->id, product, new_prod);
    }

  /* 모든 스레드가 기대한 횟수만큼 깨어났는지 검사한다. */
  for (i = 0; i < thread_cnt; i++)
    if (threads[i].iterations != iterations)
      fail ("thread %d woke up %d times instead of %d",
            i, threads[i].iterations, iterations);
  
  lock_release (&test.output_lock);
  free (output);
  free (threads);
}

/* 기능 : 지정된 주기마다 timer_sleep()으로 잠든 뒤 깨어난 순서를 기록한다.
   입출 : input : void *t_, struct sleep_thread 포인터로 변환되는 테스트 스레드 정보
          output : 반환값은 없으며 공유 출력 버퍼에 자신의 id를 기록한다. */
static void
sleeper (void *t_) 
{
  struct sleep_thread *t = t_;
  struct sleep_test *test = t->test;
  int i;

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * t->duration;
      timer_sleep (sleep_until - timer_ticks ());
      lock_acquire (&test->output_lock);
      *test->output_pos++ = t->id;
      lock_release (&test->output_lock);
    }
}
