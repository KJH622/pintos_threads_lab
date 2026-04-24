# Pintos Appendix: Synchronization — 흐름 파악용 정리

> 출처: https://casys-kaist.github.io/pintos-kaist/appendix/synchronization.html
> 목적: Project 1 시작 전에 반드시 이해해야 하는 **동기화 도구 레퍼런스**
> 이 페이지는 "구현하라"가 아니라 **"어떤 도구가 있고, 언제 쓰는지"** 알려주는 문서다.

---

## 📖 이 페이지가 뭐냐?

스레드끼리 자원을 마구 공유하면 **커널이 통째로 깨짐**.
그걸 막기 위해 Pintos가 제공하는 동기화 도구 **4가지 + 1(배리어)** 소개.

**난이도 순서 (저수준 → 고수준):**

```
인터럽트 끄기 < 세마포어 < 락 < 모니터(락+조건변수)
                                        + 최적화 배리어(특수 목적)
```

---

## 1️⃣ Disabling Interrupts — 인터럽트 끄기 (가장 원시적)

### 💡 원리
- 인터럽트를 꺼버리면 타이머 인터럽트가 못 들어옴
- → **현재 스레드가 선점(preempt) 당하지 않음**

### 📌 배경: Pintos는 "preemptible 커널"
- 커널 스레드도 **언제든** 선점될 수 있음
- 전통적인 Unix는 "nonpreemptible" — 명시적 yield 지점에서만 선점
- 그래서 Pintos는 **명시적 동기화가 꼭 필요**

### ⚠️ 거의 쓰면 안 됨!
**유일하게 써야 하는 경우:**
- **커널 스레드 ↔ 외부 인터럽트 핸들러** 간 데이터 공유
- 이유: 인터럽트 핸들러는 sleep 불가 → 락 못 잡음 → 어쩔 수 없이 인터럽트 off로 보호

### 📋 API (`include/threads/interrupt.h`)

| 함수 | 설명 |
|---|---|
| `enum intr_level` | `INTR_OFF` / `INTR_ON` 중 하나 |
| `intr_get_level()` | 현재 상태 조회 |
| `intr_set_level(level)` | 지정한 상태로 전환 (**이전 상태 반환** — 복원용) |
| `intr_enable()` | 켜기 (이전 상태 반환) |
| `intr_disable()` | 끄기 (이전 상태 반환) |

### 🔖 NMI (Non-Maskable Interrupt)
- 인터럽트를 꺼도 못 막는 특수 인터럽트 (컴퓨터가 불타는 등 긴급 상황용)
- **Pintos는 NMI 처리 안 함**

### ✅ 관용구
```c
enum intr_level old = intr_disable();
/* critical section (짧게!) */
intr_set_level(old);  // 이전 상태로 복원
```

---

## 2️⃣ Semaphores — 세마포어 (카운터 기반)

### 💡 정의
**음이 아닌 정수** + **원자적** 두 연산:
- **Down (P)**: 값이 양수 될 때까지 대기 → 1 감소
- **Up (V)**: 1 증가 (대기자 있으면 하나 깨움)

> Dijkstra가 발명. THE 운영체제에서 처음 사용.

### 🎯 쓰임새

| 초기값 | 용도 |
|---|---|
| **0** | "이벤트 한 번" 기다리기 (스레드 A가 B의 완료를 대기) |
| **1** | 자원 접근 제어 → 근데 이럴 땐 **락이 더 적절** |
| **>1** | 드묾 |

### 📝 예시 (초기값 0)

```c
struct semaphore sema;

void threadA(void) {
    sema_down(&sema);  // B가 끝날 때까지 대기
}

void threadB(void) {
    sema_up(&sema);    // 작업 완료 신호
}

void main(void) {
    sema_init(&sema, 0);
    thread_create("A", PRI_MIN, threadA, NULL);
    thread_create("B", PRI_MIN, threadB, NULL);
}
```

> A가 먼저 down 하든, B가 먼저 up 하든 **순서에 상관없이** 작동.

### 📋 API (`include/threads/synch.h`)

| 함수 | 설명 |
|---|---|
| `sema_init(sema, value)` | 초기값으로 생성 |
| `sema_down(sema)` | P 연산 — 값 양수 될 때까지 블록 |
| `sema_try_down(sema)` | 대기 없이 시도, 실패 시 false. ⚠️ tight loop 금지 |
| `sema_up(sema)` | V 연산 — **인터럽트 핸들러에서도 호출 가능** ⭐ |

### 🔧 내부 구현
- 인터럽트 끄기 + `thread_block()`/`thread_unblock()` + 대기 리스트(`lib/kernel/list.c`)로 구성

---

## 3️⃣ Locks — 락 (소유권 있는 세마포어)

### 💡 정의
초기값 1인 세마포어와 비슷하지만 **한 가지 제약 추가**:
- **락을 잡은(acquire) 스레드만 풀(release) 수 있다**
- 이 제약이 싫으면 → **락 말고 세마포어를 써라**

### ⚠️ 재귀 잠금 불가
- 이미 락을 들고 있는 스레드가 또 `lock_acquire()` 하면 **에러**

### 🔤 연산 이름 차이

| 세마포어 | 락 |
|---|---|
| down | **acquire** |
| up | **release** |

### 📋 API

| 함수 | 설명 |
|---|---|
| `lock_init(lock)` | 초기화 (소유자 없음) |
| `lock_acquire(lock)` | 잡기 (소유자 있으면 대기) |
| `lock_try_acquire(lock)` | 대기 없이 시도 |
| `lock_release(lock)` | 풀기 (**소유자만 가능**) |
| `lock_held_by_current_thread(lock)` | 현재 스레드가 소유 중인지 |

> 💡 임의의 스레드가 락을 소유하는지 조회하는 함수는 없음 — 답이 즉시 바뀔 수 있으므로 의미 없음.

---

## 4️⃣ Monitors — 모니터 (락 + 조건변수)

### 💡 정의
**세 가지 요소의 조합:**
1. **보호할 데이터**
2. **모니터 락** (monitor lock)
3. **조건 변수** (condition variable) 하나 이상

> 이론: C.A.R. Hoare 제시. Mesa OS에서 실무 적용.

### 🔄 흐름
1. 데이터 접근 전 → 락 획득 (**"모니터 안에 들어감"**)
2. 데이터 조회/수정 자유롭게
3. 작업 끝 → 락 해제

### 🎯 조건 변수의 역할
- "**어떤 조건이 참이 될 때까지** 기다리고 싶다" 는 상황 처리
- 예: "버퍼에 데이터가 도착할 때까지", "10초 이상 지날 때까지"

### 📋 API

| 함수 | 설명 |
|---|---|
| `cond_init(cond)` | 조건변수 초기화 |
| `cond_wait(cond, lock)` | **락 자동으로 놓고** 신호 기다림 → 깨면 **락 재획득** 후 리턴 |
| `cond_signal(cond, lock)` | 대기자 하나 깨움 (락 보유 상태여야 함) |
| `cond_broadcast(cond, lock)` | 대기자 전부 깨움 |

### ⚠️ 중요한 관용구: **`if` 말고 `while`**

```c
// ❌ 틀림
if (조건_아님)
    cond_wait(&cond, &lock);

// ✅ 맞음
while (조건_아님)
    cond_wait(&cond, &lock);
```

**이유:** 깨어나도 조건이 여전히 참이라는 보장이 없음 (다른 스레드가 먼저 채갈 수 있음).

---

### 🍎 고전 예제: Producer-Consumer 버퍼

```c
char buf[BUF_SIZE];
size_t n = 0, head = 0, tail = 0;
struct lock lock;
struct condition not_empty;  // 버퍼가 비어있지 않을 때 신호
struct condition not_full;   // 버퍼가 가득 차지 않을 때 신호

void put(char ch) {
    lock_acquire(&lock);
    while (n == BUF_SIZE)                // ⭐ while!
        cond_wait(&not_full, &lock);
    buf[head++ % BUF_SIZE] = ch;
    n++;
    cond_signal(&not_empty, &lock);      // 소비자 깨움
    lock_release(&lock);
}

char get(void) {
    char ch;
    lock_acquire(&lock);
    while (n == 0)                       // ⭐ while!
        cond_wait(&not_empty, &lock);
    ch = buf[tail++ % BUF_SIZE];
    n--;
    cond_signal(&not_full, &lock);       // 생산자 깨움
    lock_release(&lock);
    return ch;
}
```

---

## 5️⃣ Optimization Barriers — 최적화 배리어 (컴파일러 제어)

### 🐛 문제
컴파일러가 똑똑해서 코드를 **재배열하거나 삭제**해버림.
근데 **다른 스레드나 인터럽트 핸들러가 값을 바꿀 수 있다는 걸 컴파일러는 모름** → 버그.

### ✅ 해결
`barrier()` 매크로를 넣어서 "이 지점에서 메모리 가정을 하지 마!"라고 알려줌.
> 정의 위치: `include/threads/synch.h`

### 🎬 3가지 쓰임새

#### ① 무한루프로 최적화되는 것 막기
```c
int64_t start = ticks;
while (ticks == start)
    barrier();
// 없으면: 컴파일러가 "ticks는 안 변하는데?" → 무한루프로 최적화
```

#### ② 빈 루프 삭제 막기
```c
while (loops-- > 0)
    barrier();
// 없으면: 컴파일러가 "아무 효과도 없네" → 루프 통째로 삭제
```

#### ③ 메모리 쓰기 순서 강제
```c
timer_put_char = 'x';
barrier();                  // 이 두 줄 순서 바꾸지 마라
timer_do_put = true;
```

### ❌ 틀린 해법들

| 방법 | 왜 안 되나 |
|---|---|
| `volatile` 키워드 | 의미가 모호해서 권장 안 됨. Pintos도 거의 안 씀 |
| `lock_acquire/release`로 감싸기 | 인터럽트를 막지도 못하고, 락 내부 재배열은 여전히 일어남 |

### 💡 TIP
- 외부(다른 .c 파일) 함수 호출은 컴파일러가 **자동으로 배리어로 간주**
- → Pintos 코드에 `barrier()`가 드문 이유

---

## 🎯 그래서 내가 뭘 해야 하냐? — 흐름

### 📍 STEP 1: 의사결정 트리 (머리에 저장)

```
동기화가 필요한 상황이다. 뭘 쓸까?
  │
  ├─ 인터럽트 핸들러와 공유? ──→ 인터럽트 끄기 (최소 범위로!)
  │
  ├─ "이벤트 한 번" 기다림?  ──→ 세마포어(초기값 0)
  │
  ├─ 자원 하나 상호배제?    ──→ 락
  │
  ├─ 복잡한 조건을 기다림?   ──→ 모니터 (락 + 조건변수)
  │
  └─ 컴파일러 재배열 문제?   ──→ barrier()
```

### 📍 STEP 2: Project 1에서 어떻게 쓰이나?

| 서브과제 | 이 페이지 내용이 어떻게 쓰이나 |
|---|---|
| **Alarm Clock** | `thread_block/unblock` 직접 호출 or 세마포어 사용. 타이머 인터럽트와 데이터 공유 → **인터럽트 끄기** 필요 |
| **Priority Scheduling** | `sema_up/down`, `lock_acquire/release`의 **대기 큐를 우선순위순으로 정렬**해야 함 → `synch.c` 수정 |
| **Priority Donation** | `lock_acquire` 시 락 보유자에게 우선순위 기부, `lock_release` 시 회수 → `synch.c`에 로직 추가 |
| **Advanced Scheduler** | 전역 변수(`load_avg` 등) 보호 위해 **인터럽트 끄기** 필요 |

### 📍 STEP 3: 꼭 기억할 관용구

```c
// ✅ 조건변수는 항상 while로!
while (!조건)
    cond_wait(&cond, &lock);

// ✅ 인터럽트 끄기는 짧게, 이전 상태 복원
enum intr_level old = intr_disable();
/* critical section */
intr_set_level(old);

// ✅ 락은 acquire한 스레드가 release
lock_acquire(&lock);
/* ... */
lock_release(&lock);
```

---

## 📊 요약 비교표

| 도구 | 쓰는 때 | 특징 | 인터럽트 핸들러에서? |
|---|---|---|---|
| **인터럽트 끄기** | 핸들러와 공유 시 최후의 수단 | 단순, 강력, 남용 위험 | 이미 꺼진 상태 |
| **세마포어** | 카운팅, 이벤트 대기 | 소유자 개념 없음 | `sema_up()` O |
| **락** | 상호배제 | 소유자만 해제, 재귀 불가 | ❌ |
| **모니터** | 복잡한 조건 대기 | 락 + 조건변수, **while 패턴 필수** | ❌ |
| **barrier()** | 컴파일러 재배열 방지 | 동기화 아님, 보조 도구 | 관계없음 |

---

## 💡 기억할 핵심 5가지

1. **Pintos는 preemptible 커널** — 언제든 선점될 수 있음 → 동기화 필수
2. **인터럽트 끄기는 최후의 수단** — 핸들러와 공유할 때만 사용
3. **세마포어 < 락 < 모니터** — 뒤로 갈수록 추상화 높고 안전
4. **`cond_wait`는 반드시 `while`** — `if`로 쓰면 버그
5. **`barrier()`는 동기화 아님** — 컴파일러 최적화 방지용