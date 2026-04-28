/* 이 파일은 교육용 운영체제 Nachos의 소스 코드에서 유래했다.
   아래에는 Nachos 저작권 고지가 원문 그대로 포함되어 있다. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 기능 : 세마포어 SEMA를 VALUE 값으로 초기화한다.
          세마포어는 음수가 아닌 정수와 down(P), up(V) 원자 연산으로 구성된다.
          down은 값이 양수가 될 때까지 기다린 뒤 감소시키고,
          up은 값을 증가시키며 대기 스레드가 있으면 하나를 깨운다.
   입출 : input : struct semaphore *sema, unsigned value
          output : 반환값은 없으며 sema->value와 sema->waiters를 초기화한다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 기능 : 세마포어 SEMA의 down(P) 연산을 수행한다.
          value가 양수가 될 때까지 기다린 뒤 원자적으로 1 감소시킨다.
          대기 중에는 현재 스레드가 block될 수 있으므로 interrupt handler 안에서 호출하면 안 된다.
   입출 : input : struct semaphore *sema
          output : 반환값은 없으며 성공 시 sema->value를 감소시키고,
                   필요하면 현재 스레드를 sema->waiters에 넣고 block한다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 기능 : 세마포어 SEMA를 기다리지 않고 down(P) 시도한다.
          value가 0이 아니면 1 감소시키고, 0이면 즉시 실패한다.
          sleep하지 않으므로 interrupt handler에서도 호출할 수 있다.
   입출 : input : struct semaphore *sema
          output : 감소에 성공하면 true, value가 0이면 false를 반환한다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 기능 : 세마포어 SEMA의 up(V) 연산을 수행한다.
          value를 증가시키고 대기 스레드가 있으면 하나를 ready 상태로 깨운다.
          interrupt handler에서도 호출할 수 있다.
   입출 : input : struct semaphore *sema
          output : 반환값은 없으며 sema->value를 증가시키고 waiters에서 하나를 제거할 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	sema->value++;
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 기능 : 두 스레드가 세마포어를 이용해 번갈아 실행되는지 자체 테스트한다.
   입출 : input : 없음
          output : 반환값은 없으며 테스트 진행 메시지를 출력한다. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* 기능 : sema_self_test()에서 생성된 테스트 스레드가 두 세마포어를 번갈아 up/down한다.
   입출 : input : void *sema_, struct semaphore 배열 포인터로 변환된다
          output : 반환값은 없으며 sema[0], sema[1] 상태를 반복적으로 변경한다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* 기능 : LOCK을 초기화한다.
          lock은 동시에 하나의 스레드만 보유할 수 있으며 재귀적으로 획득할 수 없다.
          내부적으로 초기값 1인 세마포어를 사용하지만, lock은 소유자 holder를 가진다.
   입출 : input : struct lock *lock
          output : 반환값은 없으며 holder를 NULL로 만들고 내부 semaphore를 1로 초기화한다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* 기능 : LOCK을 획득한다.
          lock이 이미 사용 중이면 사용 가능해질 때까지 현재 스레드를 재운다.
          현재 스레드가 이미 같은 lock을 보유 중이면 안 된다.
   입출 : input : struct lock *lock
          output : 반환값은 없으며 성공 시 lock->holder가 현재 스레드로 변경된다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* 기능 : LOCK을 기다리지 않고 즉시 획득해 본다.
          이미 사용 중이면 sleep하지 않고 실패를 반환한다.
   입출 : input : struct lock *lock
          output : 성공하면 true를 반환하고 holder를 현재 스레드로 설정하며,
                   실패하면 false를 반환한다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 기능 : 현재 스레드가 보유한 LOCK을 해제한다.
          lock은 interrupt handler에서 획득할 수 없으므로 handler 안에서 해제하는 것도 의미가 없다.
   입출 : input : struct lock *lock
          output : 반환값은 없으며 holder를 NULL로 만들고 내부 semaphore를 up한다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 기능 : 현재 스레드가 LOCK을 보유 중인지 확인한다.
          다른 스레드의 보유 여부를 검사하는 것은 race가 될 수 있으므로 현재 스레드만 확인한다.
   입출 : input : const struct lock *lock
          output : 현재 스레드가 holder이면 true, 아니면 false를 반환한다. */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* condition variable의 waiters 목록에 들어가는 세마포어 원소. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 원소. */
	struct semaphore semaphore;         /* 대기 스레드를 재우고 깨우는 세마포어. */
};

/* 기능 : condition variable COND를 초기화한다.
          condition variable은 한 코드가 조건을 signal하고 협력 코드가 이를 기다리게 한다.
   입출 : input : struct condition *cond
          output : 반환값은 없으며 cond->waiters 목록을 초기화한다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* 기능 : LOCK을 원자적으로 해제하고 COND가 signal될 때까지 기다린 뒤 다시 LOCK을 획득한다.
          호출 전에 LOCK을 반드시 보유해야 한다. 이 구현은 Mesa 스타일 monitor이므로
          signal 송신과 수신이 하나의 원자 연산이 아니다. 따라서 wait에서 깨어난 뒤
          호출자는 보통 조건을 다시 확인해야 한다.
   입출 : input : struct condition *cond, struct lock *lock
          output : 반환값은 없으며 현재 스레드를 cond 대기열에 넣고,
                   깨어난 뒤 lock을 다시 획득한 상태로 반환한다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* 기능 : LOCK으로 보호되는 COND에서 대기 중인 스레드가 있으면 하나를 깨운다.
          호출 전에 LOCK을 반드시 보유해야 한다.
   입출 : input : struct condition *cond, struct lock *lock
          output : 반환값은 없으며 cond->waiters에서 하나를 제거해 해당 세마포어를 up한다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* 기능 : LOCK으로 보호되는 COND에서 대기 중인 모든 스레드를 깨운다.
          호출 전에 LOCK을 반드시 보유해야 한다.
   입출 : input : struct condition *cond, struct lock *lock
          output : 반환값은 없으며 cond->waiters가 빌 때까지 cond_signal()을 반복 호출한다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
