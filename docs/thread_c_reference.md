# threads/thread.c 레퍼런스

> PintOS Project 1 — Thread Management

---

## 개요

`threads/thread.c`는 PintOS 커널에서 **스레드의 생성부터 종료까지 전체 생명주기**를 관리하는 핵심 파일이다.  
스케줄러, 컨텍스트 스위칭, 스레드 상태 전환 등 멀티스레딩의 근간이 모두 여기에 있다.

---

## 스레드 상태 (Thread States)

```
생성(NEW)
   ↓  thread_create()
READY ──────────────────→ RUNNING
  ↑   next_thread_to_run()    │
  │                           │ thread_yield()
  │                           │ thread_block()
  └─────────────── READY ←────┘
                  BLOCKED ────→ (thread_unblock 호출 시 READY로)
                  DYING   ────→ thread_exit()
```

| 상태 | 설명 |
|---|---|
| `THREAD_RUNNING` | 현재 CPU를 점유하며 실행 중 |
| `THREAD_READY` | 실행 가능하지만 CPU를 기다리는 중 (`ready_list`에 있음) |
| `THREAD_BLOCKED` | 특정 이벤트(락, 슬립 등)를 기다리며 대기 중 |
| `THREAD_DYING` | 종료 처리 중, 곧 소멸됨 |

---

## 핵심 자료구조

### `struct thread`

각 스레드를 나타내는 구조체. 커널 스택의 맨 아래에 위치한다.

```
struct thread {
    tid_t              tid;        /* 스레드 고유 ID */
    enum thread_status status;     /* 현재 상태 */
    char               name[16];   /* 디버깅용 이름 */
    uint8_t           *stack;      /* 커널 스택 포인터 */
    int                priority;   /* 스케줄링 우선순위 (0~63) */
    struct list_elem   elem;       /* ready_list / 기타 리스트용 노드 */
    unsigned           magic;      /* 스택 오버플로 감지용 매직넘버 */
    /* ... Project 1 구현 시 필드 추가 ... */
};
```

> **magic 필드**: `THREAD_MAGIC` 값을 저장해두고, 스택이 오버플로되면 이 값이 깨진다. `thread_current()`에서 이를 체크해 디버깅에 활용한다.

### `ready_list`

`THREAD_READY` 상태인 스레드들이 들어있는 리스트.  
기본 구현은 **FIFO**이며, Priority Scheduling 구현 시 이 리스트의 삽입/조회 방식을 바꿔야 한다.

### `all_list`

현재 시스템에 존재하는 **모든 스레드**의 리스트. 디버깅 및 순회 목적으로 사용된다.

---

## 주요 함수 설명

### 초기화

#### `thread_init()`
```
호출 시점: 커널 부팅 초기 (main에서 가장 먼저 호출)
역할:
  - ready_list, all_list 초기화
  - 현재 실행 흐름을 initial_thread로 등록
  - tid_lock 초기화
```

#### `thread_start()`
```
호출 시점: thread_init() 이후
역할:
  - idle 스레드 생성
  - 인터럽트 활성화 → 스케줄러 동작 시작
```

---

### 스레드 생성

#### `thread_create(name, priority, function, aux)`
```
반환값: 새로 생성된 스레드의 tid
역할:
  1. palloc으로 페이지 할당 (스레드 구조체 + 커널 스택)
  2. init_thread()로 구조체 초기화
  3. kernel_thread_frame, switch_entry, switch_threads 순으로
     스택 프레임 세팅 (나중에 schedule()이 이 스택을 사용)
  4. thread_unblock() → ready_list에 추가
```

> 새 스레드는 바로 실행되지 않고 `READY` 상태로 `ready_list`에 들어간다.  
> Priority Scheduling 구현 시, 여기서 현재 스레드보다 우선순위가 높으면 즉시 yield해야 한다.

---

### 상태 전환

#### `thread_block()`
```
현재 스레드: RUNNING → BLOCKED
주의: 반드시 인터럽트가 꺼진 상태에서 호출해야 함
용도: lock_acquire(), cond_wait(), timer_sleep() 등에서 사용
```

#### `thread_unblock(t)`
```
대상 스레드: BLOCKED → READY
동작: t를 ready_list에 삽입
주의: 인터럽트가 꺼진 상태에서 호출해야 함
용도: lock_release(), cond_signal(), 타이머 인터럽트 등에서 사용
```

#### `thread_yield()`
```
현재 스레드: RUNNING → READY (자발적 CPU 양보)
동작: 현재 스레드를 ready_list 뒤에 넣고 schedule() 호출
주의: idle 스레드는 ready_list에 넣지 않음
```

#### `thread_exit()`
```
현재 스레드: RUNNING → DYING
동작:
  1. all_list에서 제거
  2. 상태를 THREAD_DYING으로 변경
  3. schedule() 호출 → 다음 스레드로 전환
  4. schedule() 내부에서 DYING 스레드 메모리 해제
```

---

### 스케줄러

#### `schedule()`
```
역할: 현재 스레드에서 다음 스레드로 컨텍스트 스위칭
동작:
  1. next_thread_to_run()으로 다음 스레드 선택
  2. switch_threads(curr, next) 호출 → CPU 레지스터 교체
  3. 이전 스레드가 DYING이면 palloc_free로 메모리 해제
주의: 반드시 인터럽트 OFF 상태에서 호출
```

#### `next_thread_to_run()`
```
역할: ready_list에서 다음 실행할 스레드 선택
기본 구현: list_pop_front() → FIFO
Priority Scheduling 구현 시: 가장 높은 우선순위 스레드 선택
```

---

### 우선순위

#### `thread_get_priority()` / `thread_set_priority(new_priority)`
```
현재 스레드의 우선순위를 반환하거나 변경한다.
Priority Donation 구현 시:
  - set 시 donated 우선순위와 비교해 더 높은 값을 실제로 사용
  - get 시 donated 상태라면 donated 값을 반환
```

---

### 유틸리티

#### `thread_current()`
```
동작: 현재 스택 포인터를 페이지 경계로 내림해 struct thread* 반환
원리: 스레드 구조체는 항상 커널 스택 페이지의 맨 아래에 위치하기 때문
```

#### `thread_tid()` / `thread_name()`
```
현재 스레드의 tid, name을 반환하는 간단한 래퍼 함수
```

#### `thread_foreach(func, aux)`
```
all_list의 모든 스레드에 func를 적용
용도: 디버깅, 전체 스레드 상태 순회
```

---

## 컨텍스트 스위칭 흐름

```
schedule()
  └─→ switch_threads(curr, next)       ← thread_switch.S (어셈블리)
        ├─ curr의 레지스터를 스택에 저장
        ├─ next의 스택 포인터로 교체
        └─ next의 레지스터 복원 후 실행 재개
              └─→ (next가 처음 실행이면) kernel_thread()
                        └─→ thread_create()에서 넘긴 function 실행
```

---

## Project 1 구현과의 연결

### 1. Alarm Clock
| 변경 전 | 변경 후 |
|---|---|
| `timer_sleep()`에서 busy-wait (`thread_yield()` 루프) | `thread_block()`으로 재우고 타이머 인터럽트에서 `thread_unblock()` |

`struct thread`에 **wake_tick** 필드를 추가하고,  
`timer_interrupt()`에서 매 tick마다 슬립 중인 스레드를 깨울지 판단한다.

---

### 2. Priority Scheduling
| 함수 | 수정 내용 |
|---|---|
| `thread_unblock()` | `ready_list`에 우선순위 순 삽입 |
| `thread_yield()` | `ready_list`에 우선순위 순 삽입 |
| `next_thread_to_run()` | 가장 높은 우선순위 스레드 선택 |
| `thread_create()` | 새 스레드 우선순위 > 현재 스레드면 즉시 yield |
| `thread_set_priority()` | 변경 후 yield 여부 판단 |

---

### 3. Priority Donation
`struct thread`에 추가가 필요한 필드:

```
struct thread {
    /* ... 기존 필드 ... */
    int base_priority;           /* 원래 우선순위 (donation 전) */
    struct lock *wait_on_lock;   /* 현재 기다리는 락 */
    struct list donations;       /* 나에게 donation한 스레드 목록 */
    struct list_elem donor_elem; /* donations 리스트용 노드 */
};
```

Donation 흐름:
```
lock_acquire(L) 호출
  → L을 가진 holder의 우선순위가 낮으면 donation
  → holder가 또 다른 락을 기다리면 체인을 따라 재귀 donation (nested donation)

lock_release(L) 호출
  → L에 연결된 donation 취소
  → 남은 donation 중 최댓값 or base_priority로 복원
```

---

## 자주 하는 실수

- `thread_block()` / `thread_unblock()`을 인터럽트 ON 상태에서 호출하면 레이스 컨디션 발생 → 반드시 `intr_disable()` 후 호출
- `ready_list`를 직접 조작할 때 `list_entry()` 매크로로 `struct thread *`를 꺼내야 함
- `thread_current()->magic != THREAD_MAGIC`이면 스택 오버플로 → 구조체에 필드를 너무 많이 추가하지 않도록 주의
- `thread_yield()`는 인터럽트 핸들러 안에서 호출하면 안 됨 (대신 `intr_yield_on_return()` 사용)

---

## 관련 파일

| 파일 | 관계 |
|---|---|
| `threads/thread.h` | `struct thread` 정의, 함수 선언 |
| `threads/thread_switch.S` | `switch_threads()` 어셈블리 구현 |
| `threads/synch.c` | lock, semaphore, condition variable — thread_block/unblock 사용 |
| `devices/timer.c` | `timer_interrupt()` — Alarm Clock 구현 시 수정 |
| `threads/interrupt.c` | 인터럽트 on/off, `intr_yield_on_return()` |