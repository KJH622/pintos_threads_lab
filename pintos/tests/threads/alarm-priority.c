/* alarm clock이 여러 스레드를 깨울 때 높은 우선순위 스레드가 먼저 실행되는지 검사한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func alarm_priority_thread;
static int64_t wake_time;
static struct semaphore wait_sema;

/* 기능 : 같은 wake time을 가진 스레드들이 priority 내림차순으로 실행되는지 테스트한다.
   입출 : input : 없음
          output : 반환값은 없으며 각 스레드의 wakeup 메시지가 기대 순서와 다르면 테스트가 실패한다. */
void
test_alarm_priority (void) 
{
  int i;
  
  /* 이 테스트는 MLFQS 모드에서 동작하지 않는다. */
  ASSERT (!thread_mlfqs);

  wake_time = timer_ticks () + 5 * TIMER_FREQ;
  sema_init (&wait_sema, 0);
  
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, alarm_priority_thread, NULL);
    }

  thread_set_priority (PRI_MIN);

  for (i = 0; i < 10; i++)
    sema_down (&wait_sema);
}

/* 기능 : 지정된 wake_time까지 잠든 뒤 깨어난 순서를 메시지로 출력하고 semaphore로 완료를 알린다.
   입출 : input : void *aux, 사용하지 않는 보조 인자
          output : 반환값은 없으며 wait_sema를 up해서 메인 테스트 스레드에 완료를 알린다. */
static void
alarm_priority_thread (void *aux UNUSED) 
{
  /* 현재 tick이 바뀔 때까지 busy-wait하여 tick 시작 지점에 맞춘다. */
  int64_t start_time = timer_ticks ();
  while (timer_elapsed (start_time) == 0)
    continue;

  /* timer tick의 시작 지점임을 알고 있으므로 시간 확인과 timer interrupt 사이의 race를 줄인 상태로
     timer_sleep()을 호출할 수 있다. */
  timer_sleep (wake_time - timer_ticks ());

  /* 깨어난 뒤 현재 스레드 이름을 출력한다. */
  msg ("Thread %s woke up.", thread_name ());

  sema_up (&wait_sema);
}
