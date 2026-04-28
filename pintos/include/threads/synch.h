#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어.
   기능 : 공유 자원의 사용 가능 개수를 세고, 값이 0이면 스레드를 대기시킨다.
   입출 : input : sema_init(), sema_down(), sema_up()에서 포인터로 전달된다
          output : value와 waiters가 동기화 상태에 따라 변경된다. */
struct semaphore {
	unsigned value;             /* 현재 사용 가능한 자원 개수. */
	struct list waiters;        /* 대기 중인 스레드 목록. */
};

/* 기능 : 세마포어를 초기 값으로 초기화한다.
   입출 : input : struct semaphore 포인터와 unsigned 초기값
          output : 반환값은 없으며 세마포어 내부 상태를 초기화한다. */
void sema_init (struct semaphore *, unsigned value);
/* 기능 : 세마포어 값을 얻을 때까지 대기하고 값을 1 감소시킨다.
   입출 : input : struct semaphore 포인터
          output : 반환값은 없으며 필요하면 현재 스레드를 block 상태로 변경한다. */
void sema_down (struct semaphore *);
/* 기능 : 세마포어 값을 즉시 얻을 수 있는 경우에만 1 감소시킨다.
   입출 : input : struct semaphore 포인터
          output : 성공 여부를 bool 값으로 반환하고 성공 시 value를 감소시킨다. */
bool sema_try_down (struct semaphore *);
/* 기능 : 세마포어 값을 1 증가시키고 대기 중인 스레드 하나를 깨운다.
   입출 : input : struct semaphore 포인터
          output : 반환값은 없으며 value와 waiters 상태를 변경한다. */
void sema_up (struct semaphore *);
/* 기능 : 세마포어 기본 동작을 자체 테스트한다.
   입출 : input : 없음
          output : 반환값은 없으며 테스트 로그를 출력한다. */
void sema_self_test (void);

/* 락.
   기능 : 한 번에 하나의 스레드만 임계 구역을 소유하게 한다.
   입출 : input : lock 관련 함수에 포인터로 전달된다
          output : holder와 내부 semaphore 상태가 lock 획득/해제에 따라 변경된다. */
struct lock {
	struct thread *holder;      /* 락을 보유 중인 스레드. 디버깅과 소유권 확인에 사용된다. */
	struct semaphore semaphore; /* 접근 제어에 사용하는 이진 세마포어. */
};

/* 기능 : lock 구조체를 사용 가능한 상태로 초기화한다.
   입출 : input : struct lock 포인터
          output : 반환값은 없으며 holder와 내부 semaphore를 초기화한다. */
void lock_init (struct lock *);
/* 기능 : lock을 획득하고, 이미 사용 중이면 획득 가능할 때까지 대기한다.
   입출 : input : struct lock 포인터
          output : 반환값은 없으며 성공 시 현재 스레드가 holder가 된다. */
void lock_acquire (struct lock *);
/* 기능 : lock을 즉시 획득할 수 있는 경우에만 획득을 시도한다.
   입출 : input : struct lock 포인터
          output : 성공 여부를 bool 값으로 반환하고 성공 시 holder를 변경한다. */
bool lock_try_acquire (struct lock *);
/* 기능 : 현재 스레드가 보유한 lock을 해제한다.
   입출 : input : struct lock 포인터
          output : 반환값은 없으며 holder를 비우고 대기 스레드를 깨울 수 있다. */
void lock_release (struct lock *);
/* 기능 : 현재 스레드가 해당 lock을 보유 중인지 확인한다.
   입출 : input : const struct lock 포인터
          output : 보유 중이면 true, 아니면 false를 반환한다. */
bool lock_held_by_current_thread (const struct lock *);

/* 조건 변수.
   기능 : 특정 조건이 만족될 때까지 lock과 함께 스레드를 대기시킨다.
   입출 : input : condition 관련 함수에 포인터로 전달된다
          output : waiters 목록이 대기/신호 동작에 따라 변경된다. */
struct condition {
	struct list waiters;        /* 조건 신호를 기다리는 대기자 목록. */
};

/* 기능 : condition 구조체를 초기화한다.
   입출 : input : struct condition 포인터
          output : 반환값은 없으며 waiters 목록을 초기화한다. */
void cond_init (struct condition *);
/* 기능 : lock을 원자적으로 해제하고 조건 신호가 올 때까지 대기한 뒤 다시 lock을 획득한다.
   입출 : input : struct condition 포인터와 struct lock 포인터
          output : 반환값은 없으며 현재 스레드가 대기 후 다시 lock을 보유하게 된다. */
void cond_wait (struct condition *, struct lock *);
/* 기능 : condition에서 대기 중인 스레드 하나를 깨운다.
   입출 : input : struct condition 포인터와 struct lock 포인터
          output : 반환값은 없으며 waiters에서 하나의 대기자를 제거해 깨운다. */
void cond_signal (struct condition *, struct lock *);
/* 기능 : condition에서 대기 중인 모든 스레드를 깨운다.
   입출 : input : struct condition 포인터와 struct lock 포인터
          output : 반환값은 없으며 waiters의 모든 대기자를 깨운다. */
void cond_broadcast (struct condition *, struct lock *);

/* 최적화 장벽.
 *
 * 컴파일러가 barrier() 앞뒤의 메모리 연산 순서를 재배치하지 못하게 한다.
 * 자세한 내용은 reference guide의 "Optimization Barriers"를 참고한다. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
