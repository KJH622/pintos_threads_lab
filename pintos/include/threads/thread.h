#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* 스레드 생명 주기의 상태 값. */
enum thread_status {
	THREAD_RUNNING,     /* 현재 CPU에서 실행 중인 스레드. */
	THREAD_READY,       /* 실행 중은 아니지만 실행 준비가 된 스레드. */
	THREAD_BLOCKED,     /* 특정 이벤트를 기다리며 block된 스레드. */
	THREAD_DYING        /* 곧 제거될 스레드. */
};

/* 스레드 식별자 타입.
   필요하면 다른 타입으로 재정의할 수 있다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* tid_t 오류 값. */

#define PRI_MIN 0                       /* 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. */
#define PRI_MAX 63                      /* 가장 높은 우선순위. */

/* 커널 스레드 또는 사용자 프로세스.
 *
 * 각 thread 구조체는 독립된 4 kB 페이지에 저장된다. thread 구조체는
 * 페이지의 가장 아래(offset 0)에 위치하고, 나머지 공간은 커널 스택으로
 * 사용된다. 커널 스택은 페이지의 가장 위(offset 4 kB)에서 아래 방향으로
 * 자란다. 구조는 다음과 같다.
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택              |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |          아래로 성장            |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 이 구조에서 중요한 점은 두 가지다.
 *
 *    1. `struct thread'가 너무 커지면 안 된다. 커지면 커널 스택이 사용할
 *       공간이 부족해진다. 기본 `struct thread'는 작은 크기이며, 가능하면
 *       1 kB보다 충분히 작게 유지해야 한다.
 *
 *    2. 커널 스택이 너무 커져도 안 된다. 스택이 넘치면 thread 상태를
 *       덮어써서 손상시킨다. 따라서 커널 함수 안에서는 큰 구조체나 배열을
 *       non-static 지역 변수로 두지 말고, 필요하면 malloc()이나
 *       palloc_get_page()로 동적 할당해야 한다.
 *
 * 두 문제의 첫 증상은 보통 thread_current()의 assertion 실패다.
 * thread_current()는 실행 중인 thread의 `magic' 값이 THREAD_MAGIC인지
 * 확인한다. 스택 오버플로는 보통 이 값을 바꾸므로 assertion이 발생한다. */
/* `elem' 멤버는 두 가지 용도로 쓰인다. ready 상태일 때는 thread.c의
 * 실행 대기 큐 원소가 되고, blocked 상태일 때는 synch.c의 semaphore
 * 대기 목록 원소가 된다. 두 상태는 동시에 발생하지 않으므로 같은 elem을
 * 공유해서 사용할 수 있다. */
struct thread {
	/* thread.c가 관리하는 필드. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 디버깅용 스레드 이름. */
	int priority;                       /* 스레드 우선순위. */
   int64_t outTick;                    /* 스레드 wakeup time. */

	/* thread.c와 synch.c가 공유하는 필드. */
	struct list_elem elem;              /* 리스트 원소. */

#ifdef USERPROG
	/* userprog/process.c가 관리하는 필드. */
	uint64_t *pml4;                     /* Page Map Level 4 테이블. */
#endif
#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리용 보조 페이지 테이블. */
	struct supplemental_page_table spt;
#endif

	/* thread.c가 관리하는 필드. */
	struct intr_frame tf;               /* 스레드 전환에 필요한 실행 문맥 정보. */
	unsigned magic;                     /* 스택 오버플로 감지용 값. */
};


/* false이면 기본 round-robin 스케줄러를 사용한다.
   true이면 multi-level feedback queue scheduler를 사용한다.
   커널 명령행 옵션 "-o mlfqs"로 제어된다. */
extern bool thread_mlfqs;

/* 기능 : 스레드 시스템을 초기화한다.
   입출 : input : 없음
          output : 반환값은 없으며 현재 실행 흐름을 초기 thread로 등록한다. */
void thread_init (void);
/* 기능 : idle thread를 만들고 선점형 스케줄링을 시작한다.
   입출 : input : 없음
          output : 반환값은 없으며 인터럽트를 활성화하고 idle thread 준비를 기다린다. */
void thread_start (void);

/* 기능 : timer interrupt마다 스레드 통계와 time slice를 갱신한다.
   입출 : input : 없음
          output : 반환값은 없으며 필요하면 interrupt 반환 시 yield를 예약한다. */
void thread_tick (void);
/* 기능 : 스레드 실행 통계를 출력한다.
   입출 : input : 없음
          output : 반환값은 없으며 idle/kernel/user tick 수를 출력한다. */
void thread_print_stats (void);

/* 스레드가 실행할 함수 타입. aux 포인터 하나를 입력으로 받고 반환값은 없다. */
typedef void thread_func (void *aux);
/* 기능 : 새 커널 스레드를 생성해 ready 상태로 만든다.
   입출 : input : 이름, 초기 우선순위, 실행 함수 포인터, 함수에 넘길 aux 포인터
          output : 성공 시 tid_t 스레드 ID, 실패 시 TID_ERROR를 반환한다. */
tid_t thread_create (const char *name, int priority, thread_func *, void *);

/* 기능 : 현재 스레드를 block 상태로 바꿔 스케줄 대상에서 제외한다.
   입출 : input : 없음
          output : 반환값은 없으며 현재 스레드 상태와 스케줄링 흐름을 변경한다. */
void thread_block (void);
/* 기능 : blocked 상태의 스레드를 ready 상태로 전환한다.
   입출 : input : struct thread 포인터
          output : 반환값은 없으며 대상 스레드를 ready list에 넣는다. */
void thread_unblock (struct thread *);

/* 기능 : 현재 실행 중인 스레드 구조체를 반환한다.
   입출 : input : 없음
          output : struct thread 포인터를 반환한다. */
struct thread *thread_current (void);
/* 기능 : 현재 스레드의 tid를 반환한다.
   입출 : input : 없음
          output : tid_t 타입의 스레드 ID를 반환한다. */
tid_t thread_tid (void);
/* 기능 : 현재 스레드 이름을 반환한다.
   입출 : input : 없음
          output : const char 포인터 형태의 이름 문자열을 반환한다. */
const char *thread_name (void);

/* 기능 : 현재 스레드를 종료하고 제거 대상으로 표시한다.
   입출 : input : 없음
          output : 반환하지 않으며 다른 스레드로 스케줄링된다. */
void thread_exit (void) NO_RETURN;
/* 기능 : 현재 스레드가 CPU를 양보하고 ready 상태로 돌아간다.
   입출 : input : 없음
          output : 반환값은 없으며 스케줄러가 다른 스레드를 실행할 수 있다. */
void thread_yield (void);

/* 기능 : 현재 스레드의 우선순위를 조회한다.
   입출 : input : 없음
          output : int 타입의 현재 우선순위를 반환한다. */
int thread_get_priority (void);
/* 기능 : 현재 스레드의 우선순위를 설정한다.
   입출 : input : int 타입의 새 우선순위
          output : 반환값은 없으며 현재 스레드 priority 값을 변경한다. */
void thread_set_priority (int);

/* 기능 : 현재 스레드의 nice 값을 조회한다.
   입출 : input : 없음
          output : int 타입의 nice 값을 반환한다. */
int thread_get_nice (void);
/* 기능 : 현재 스레드의 nice 값을 설정한다.
   입출 : input : int 타입의 nice 값
          output : 반환값은 없으며 MLFQS 계산에 쓰일 nice 값을 변경한다. */
void thread_set_nice (int);
/* 기능 : 현재 스레드의 recent_cpu 값을 100배 스케일로 조회한다.
   입출 : input : 없음
          output : int 타입의 recent_cpu * 100 값을 반환한다. */
int thread_get_recent_cpu (void);
/* 기능 : 시스템 load_avg 값을 100배 스케일로 조회한다.
   입출 : input : 없음
          output : int 타입의 load_avg * 100 값을 반환한다. */
int thread_get_load_avg (void);

/* 기능 : 저장된 interrupt frame으로 레지스터를 복원하고 iretq로 실행을 재개한다.
   입출 : input : struct intr_frame 포인터
          output : 일반적인 함수처럼 반환하지 않고 지정된 실행 문맥으로 전환한다. */
void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
