/* timer_sleep(0)이 즉시 반환되는지 테스트한다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

/* 기능 : 0 tick sleep 요청이 block 없이 즉시 완료되는지 확인한다.
   입출 : input : 없음
          output : 반환값은 없으며 정상 반환 시 pass()를 호출한다. */
void
test_alarm_zero (void) 
{
  timer_sleep (0);
  pass ();
}
