# Alarm Clock 개념 학습 + 삽질 기록

> 과외 선생님과 대화하면서 배운 것들을 정리.
> 개념 이해 + 삽질 포인트 위주로 기록.

---

## 🗺️ init.c 흐름 따라가기

### main() 호출 순서 (핵심만)

```
main()
 ├─ ... (bss_init 등 메모리 초기화)
 ├─ thread_init()        ← 2번째로 호출됨
 ├─ ...
 ├─ timer_init()         ← thread_init() 보다 뒤
 ├─ ...
 ├─ input_init()
 ├─ thread_start()       ← input_init() 이후 호출
 └─ ...
```

### 왜 이 순서가 중요한가?

- **`thread_init()` → `timer_init()` 순서인 이유**
  - 타이머 인터럽트가 발생하면 "지금 실행 중인 스레드"를 참조해야 함
  - 스레드 시스템이 먼저 세팅되지 않으면, 인터럽트 핸들러가 존재하지 않는 스레드를 건드리게 됨
  - 순서가 뒤바뀌면 → tick이 증가하면서 인터럽트가 발생하는데 스레드 정보가 없어서 터짐

- **`thread_start()`의 역할**
  - 여기서 idle thread가 생성되고 스케줄러가 본격 시작됨
  - 이 이후부터 timer_interrupt()가 의미 있는 동작을 할 수 있음

---

## ⚡ 인터럽트 & Tick 개념

### 인터럽트란?

하드웨어(또는 소프트웨어)가 CPU에게 보내는 **"잠깐, 나 좀 봐줘!"** 신호.

```
CPU: 코드 열심히 실행 중...
타이머 하드웨어: 딩동! (인터럽트 발생)
CPU: 하던 거 잠깐 멈추고 → timer_interrupt() 실행 → 다시 원래 하던 곳으로 복귀
```

- 인터럽트는 **내가 원할 때 발생하는 게 아님** — 하드웨어가 알아서 때림
- 어떤 코드를 실행하던 도중에도 언제든 끼어들 수 있음
- 그래서 인터럽트 핸들러 안에서는 조심해야 할 것들이 많음 (sleep, lock 등 금지)

### Tick이란?

타이머 하드웨어가 CPU에게 보내는 신호의 **단위**.

- PintOS에서 `TIMER_FREQ = 100` → 1초에 100번 tick 발생
- 즉, **1 tick = 10ms**
- `timer_ticks()`를 호출하면 부팅 이후 몇 번 tick이 됐는지 알 수 있음
- tick은 시간을 표현하는 방법 — "100 ticks 자" = "1초 자"

---

## 🔧 timer_init() 내부 파헤치기

### 등장하는 낯선 것들

**`uint16_t`란?**
- 부호 없는(unsigned) 16비트 정수 타입. 범위: 0 ~ 65535
- 타이머 하드웨어(PIT: Programmable Interval Timer)에 설정값을 넣을 때 딱 16비트가 필요함
- 하드웨어 규격에 맞춰서 쓰는 타입

**`TIMER_FREQ`란?**
- 1초에 타이머 인터럽트를 몇 번 발생시킬지 정하는 상수
- PintOS에서는 100 → 1초에 100번 tick

**`outb()`란?**
- CPU가 하드웨어 포트에 값을 직접 쓰는 명령어 (out byte)
- "야 타이머 하드웨어야, 1초에 100번 신호 보내줘" 하고 설정값을 전달하는 것
- timer_init() 안에 있는 이유: 타이머 하드웨어를 초기화하는 과정이기 때문

### `intr_register_ext()`란?

"특정 인터럽트 번호가 발생하면, 이 함수를 실행해줘"라고 OS에 등록하는 함수.

| 인자 | 값 | 의미 |
|------|-----|------|
| 첫 번째 | `0x20` | 인터럽트 벡터 번호 (IRQ0 = 타이머) |
| 두 번째 | `timer_interrupt` | 인터럽트 발생 시 호출할 함수 |
| 세 번째 | 인터럽트 레벨 | 핸들러 실행 중 다른 인터럽트 허용 여부 |
| 네 번째 | 이름 문자열 | 디버깅용 이름 |

**`0x20`이 뭔가?**
- x86 아키텍처에서 IRQ0(타이머 인터럽트)는 인터럽트 벡터 번호 32번 = 0x20에 매핑됨
- 하드웨어가 타이머 신호를 보낼 때 CPU는 "0x20번 인터럽트다!" 하고 등록된 핸들러를 찾아 실행

### ❗ `timer_interrupt` vs `timer_interrupt()` — 중요한 차이!

```
timer_interrupt    → 함수의 "주소(포인터)"를 넘김  (등록용)
timer_interrupt()  → 함수를 "지금 당장 실행"      (호출)
```

`intr_register_ext()`는 "나중에 인터럽트가 터지면 이 함수 주소로 찾아가서 실행해줘"라는 의미.
지금 실행하는 게 아니라 **주소를 저장**하는 것이므로 `()`를 붙이면 안 됨.

비유: 친구한테 "맛집 전화번호 저장해줘"라고 하는 것 vs "지금 바로 전화 걸어줘"는 다름.

---

## 🔄 timer_interrupt() 역할

OS의 **심장박동** 역할. 매 10ms마다 하드웨어가 자동으로 호출.

```
timer_interrupt() 호출 흐름:
  1. ticks++           — 전역 tick 카운터 증가
  2. thread_tick()     — 스케줄러에게 시간 경과 알림
  3. (구현 후 추가)     — sleep_list에서 깰 스레드 찾기
```

### thread_tick() 안에서 하는 일

- `thread_current()`로 지금 실행 중인 스레드 파악
- 해당 스레드가 CPU를 `TIME_SLICE`(4 ticks = 40ms) 이상 썼으면
  → `intr_yield_on_return()` 호출 (yield 예약)

### 관련 개념 정리

| 개념 | 설명 |
|------|------|
| `thread_current()` | 지금 CPU에서 실행 중인 스레드 포인터 반환. 비유: "지금 발표대 앞에 있는 학생이 누구야?" |
| `idle_thread` | ready_list가 비었을 때 실행되는 특수 스레드. CPU는 항상 뭔가를 실행해야 하기 때문에 존재 |
| `idle_ticks` | idle thread가 CPU를 쓴 시간 (CPU가 얼마나 놀았나) |
| `kernel_ticks` | 커널 코드가 CPU를 쓴 시간 |
| `user_ticks` | 유저 프로그램이 CPU를 쓴 시간 |
| `TIME_SLICE` | 4 ticks (= 40ms). 한 스레드가 CPU를 독점할 수 있는 최대 시간 |
| `intr_yield_on_return()` | 인터럽트 핸들러가 끝난 뒤 yield하도록 예약하는 함수 |

---

## 😴 busy-wait 문제와 sleep_list 설계

### 기존 timer_sleep()의 문제

```
timer_sleep() {
    start = timer_ticks();
    ASSERT(intr_get_level() == INTR_ON);  // 인터럽트 켜져 있어야 함
    while (timer_elapsed(start) < ticks)  // busy-wait
        thread_yield();                   // ready_list 맨 뒤로
}
```

- `timer_elapsed(start)` = `timer_ticks() - start` (start 이후 흐른 ticks)
- `thread_yield()`는 스레드를 **ready_list 맨 뒤**에 넣음
- 잠들어야 할 스레드가 ready_list에서 계속 깨어 있으면서 CPU를 받을 때마다 조건만 확인하고 다시 내려놓는 것 반복
- **CPU 낭비 심각**

### thread_yield() vs thread_block() 비교

| 함수 | 스레드 상태 | ready_list 포함 | CPU 받음? |
|------|------------|----------------|-----------|
| `thread_yield()` | READY | ✅ 포함 | 계속 받음 (busy-wait) |
| `thread_block()` | BLOCKED | ❌ 제외 | 받지 않음 |

### sleep_list가 필요한 이유

- `ready_list` = 실행 대기 중인 스레드 목록
- 잠든 스레드는 실행 대기가 아님 → ready_list에 있으면 안 됨
- 하지만 `thread_block()`은 스케줄러 대상에서만 빼줄 뿐, **언제 깨울지 추적하지 않음**
- 따라서 우리가 직접 `sleep_list`를 만들어 "언제 깨울지(wakeup_tick)"를 관리해야 함

### ❗ 삽입 순서 — 헷갈리기 쉬운 포인트

```
❌ 잘못된 이해: thread_block() → sleep_list 삽입
✅ 올바른 순서: sleep_list 삽입 → thread_block() 호출
```

`thread_block()`은 sleep_list를 모름. 우리가 직접 삽입하고 나서 block 호출.

### sleep_list 오름차순 정렬이 필요한 이유

```
정렬 안 했을 때: 깨울 스레드 찾으려면 끝까지 다 봐야 함 → O(n)
오름차순 정렬:  앞 원소가 아직이면 뒤는 볼 필요 없음 → 즉시 break → O(k)
```

`timer_interrupt()`는 매 10ms마다 호출되므로 이 최적화가 실제로 의미 있음.

---

## ⚠️ Race Condition — 인터럽트를 꺼야 하는 이유

인터럽트는 **어떤 코드를 실행하는 도중에도 언제든 끼어들 수 있음.**

```
우리 코드                         timer_interrupt (언제든 끼어들 수 있음)
──────────────────────────────    ──────────────────────────────────────
1. wakeup_tick 설정
2. sleep_list에 삽입 시작...
   (리스트 포인터 수정 중)
                        ← 여기서 인터럽트 발생!
                                   sleep_list 순회
                                   리스트가 반만 수정된 상태
                                   포인터 꼬임 → 💥 터짐
3. 삽입 완료
4. thread_block() 호출
```

따라서 "sleep_list 삽입 + thread_block()"은 반드시 **인터럽트 비활성화 상태**에서 한 묶음으로 처리해야 함.

---

## 🔌 list_insert_ordered() 사용법

```c
void list_insert_ordered(struct list *list,
                         struct list_elem *elem,
                         list_less_func *less,
                         void *aux);
```

| 인자 | 타입 | 의미 |
|------|------|------|
| `list` | `struct list *` | 삽입할 리스트 |
| `elem` | `struct list_elem *` | 삽입할 원소 |
| `less` | `list_less_func *` | 비교 함수 포인터 (a < b 이면 true) |
| `aux` | `void *` | 비교 함수에 넘길 부가 데이터 (보통 NULL) |

비교 함수 시그니처:
```c
bool 비교함수명(const struct list_elem *a,
               const struct list_elem *b,
               void *aux);
```

wakeup_tick 오름차순 정렬이라면: a의 wakeup_tick < b의 wakeup_tick 이면 true 반환.

---

## 🏗️ 설계 결정: timer.c에 몰아넣기 (콜 체인 방식)

### 최종 결정: timer.c 방식

| 파일 | 역할 |
|------|------|
| `thread.h` | `wakeup_tick` 필드 추가만 |
| `timer.c` | `sleep_list` 선언 + 초기화, 재우기 + 깨우기 전부 |

### 왜 timer.c 방식을 선택했나?

main()에서 콜 체인을 따라가면:

```
main()
  ├─ thread_init()     → 스레드 기본 초기화만
  ├─ timer_init()      → sleep_list 초기화 여기서
  └─ (테스트 실행)
       └─ timer_sleep()      → 스레드 재우기
          timer_interrupt()  → 스레드 깨우기
```

sleep_list / 재우기 / 깨우기가 **전부 timer.c 안에서 완결**됨.
파일을 넘나들 필요가 없어서 콜 체인 추적이 명확함.

### thread.c 방식과 비교

| | thread.c 방식 | timer.c 방식 |
|--|--------------|-------------|
| 콜 체인 | 파일 왔다갔다 | timer.c 안에서 완결 |
| 설계 | 관심사 분리 (정석) | 단순하고 추적이 쉬움 |
| 학습 관점 | 복잡 | 흐름 파악에 유리 |

### 변경 이력

- thread.c에 추가했던 `static struct list sleep_list`와 `list_init(&sleep_list)` → **삭제**
- thread_sleep() 별도 구현 없이 timer_sleep() 안에서 직접 처리

---

## 🔢 int64_t 개념

### int64_t가 뭐야?

- `int` = 정수(integer)
- `64` = 64비트
- `t` = type (타입이라는 의미)
- 합치면: **64비트 크기의 정수 타입**

비트 수에 따라 담을 수 있는 숫자 범위가 달라짐:

| 타입 | 비트 수 | 최대값 | 한계 |
|------|---------|--------|------|
| `int` (int32_t) | 32비트 | 약 21억 | 248일 치 tick |
| `int64_t` | 64비트 | 약 920경 | 수십억 년 치 tick |

### 왜 wakeup_tick이 timer_ticks()의 반환값을 받는 그릇인가?

`timer_sleep(ticks)`가 호출되면 "지금 이 순간부터 ticks 만큼 후에 깨워줘"라는 뜻이야.

```
wakeup_tick = timer_ticks() + ticks
              ↑ 지금 몇 번째 tick?   ↑ 얼마나 더 자야 해?
```

이후 `timer_interrupt()`가 매 tick마다 이걸 비교:
```
현재 ticks >= wakeup_tick  →  깨울 시간이 됐다!
현재 ticks <  wakeup_tick  →  아직 아니다
```

`timer_ticks()`가 `int64_t`를 반환하기 때문에, 그 값을 담는 `wakeup_tick`도 반드시 `int64_t`여야 타입이 맞음.

### 32비트로 하면 왜 오버플로우가 생겨?

오버플로우 = 담을 수 있는 숫자 범위를 넘어서면 반대편으로 튀어나오는 현상.

비유: 자동차 주행거리 계기판이 최대 999,999km인데 1km 더 가면 000,000으로 돌아오는 것.

```
int32_t 최대값: 2,147,483,647

PintOS tick 속도: 1초에 100번
→ 2,147,483,647 ÷ 100 = 21,474,836초
→ = 약 248일

248일 이후:
ticks = 2,147,483,647 (최대)
ticks + 1 = -2,147,483,648  ← 음수로 튀어나옴! 💥

비교: wakeup_tick(양수) <= ticks(음수) → 조건이 이상하게 판단됨
→ 자야 할 스레드가 갑자기 깨어나거나, 영원히 안 깨어나거나
```

`int64_t`는 최대값이 약 920경이라 PintOS 수명 동안 절대 오버플로우 없음.

---

## 🧱 전역 선언 & static — 왜 이렇게 쓰나?

### sleep_list를 전역으로 선언하는 이유

`sleep_list`는 두 함수가 공유해야 함:

```
thread_sleep()    → sleep_list에 삽입
timer_interrupt() → sleep_list에서 꺼내기(unblock)
```

지역변수로 선언하면 그 함수 안에서만 보임. 여러 함수가 공유하려면 전역(함수 밖)에 선언해야 함.

### static을 붙이는 이유

`static`을 함수 밖(전역 위치)에 붙이면 의미가 달라짐:

| 선언 방식 | 어디서 접근 가능? |
|-----------|-----------------|
| `struct list sleep_list` | 모든 파일에서 접근 가능 (위험) |
| `static struct list sleep_list` | thread.c 안에서만 접근 가능 ✅ |

`sleep_list`는 thread.c 내부에서만 관리하면 충분하고, 다른 파일이 직접 건드리면 안 됨.
`static`으로 "이 파일 전용"으로 묶어두는 것이 좋은 설계.

### thread_sleep()은 왜 thread.h에 선언해야 해?

`timer.c`에서 `thread_sleep()`을 호출해야 함.
C언어에서 **다른 파일의 함수를 쓰려면 반드시 헤더에 선언이 있어야 함**.
`timer.c`는 `thread.h`를 include하고 있으므로, 거기다 선언해두면 `timer.c`가 자동으로 찾아감.

---

## 📌 구현 체크리스트

- [x] `thread.h` — `int64_t wakeup_tick` 필드 추가
- [x] `timer.c` — `static struct list sleep_list` 선언 + `timer_init()`에서 초기화
- [x] `timer.c` — `timer_sleep()` busy-wait 제거 + block 방식으로 전환
- [ ] `timer.c` — `timer_interrupt()` sleep_list 순회 + unblock 추가

---

## 🔨 삽질 기록

### ✅ thread.h — wakeup_tick 추가

`struct thread` 안에 `int64_t wakeup_tick` 추가.
- `int64_t`를 쓰는 이유: `timer_ticks()` 반환 타입이 `int64_t`이기 때문에 맞춰야 함
- 32비트 int로 하면 248일 이후 오버플로우 발생 위험
- `wakeup_tick = timer_ticks() + ticks` 형태로 계산해서 저장

### ❌ thread.c — sleep_list 시도 후 철회

처음엔 thread.c에 선언 시도:
```c
static struct list sleep_list;      // 전역 선언
list_init(&sleep_list);             // thread_init() 안에서 초기화
```
콜 체인 관점에서 timer.c에 몰아넣는 방식이 더 명확하다고 판단해 **삭제**.

삽질 포인트:
- `static int64_t sleep_list();` 로 잘못 선언 → `int64_t`는 정수 타입, `()`는 함수 선언이 됨
- 올바른 선언: `static struct list sleep_list;`

### ❌ 삽질 1 — sleep_list 타입 잘못 선언

```c
static int64_t sleep_list();   // ❌ 잘못됨
```
- `int64_t`는 정수 타입 → 리스트가 아님
- `()`를 붙이면 변수가 아니라 함수 선언이 됨
- 올바른 선언: `static struct list sleep_list;`

### ❌ 삽질 2 — list_entry()에서 destruction_req vs elem 혼동

`struct thread` 안에 list_elem이 두 개 있음:
```
struct list_elem elem;             ← 일반 용도 (sleep_list, ready_list 등)
struct list_elem destruction_req;  ← 스레드 소멸 요청 전용
```
sleep_list에 삽입하고 꺼낼 때는 반드시 `elem`을 써야 함.
`list_entry(e, struct thread, elem)` — destruction_req 아님.

### ✅ timer.c — sleep_list 선언 + timer_init() 초기화

```c
static struct list sleep_list;   // 파일 상단 전역 선언
```
```c
void timer_init(void) {
    list_init(&sleep_list);      // 가장 먼저 초기화
    ...
}
```
- `static`을 붙인 이유: timer.c 안에서만 관리. 다른 파일이 직접 접근하면 안 됨
- `timer_init()`에서 초기화하는 이유: `timer_sleep()` 호출 전에 반드시 준비되어 있어야 함

---

### ✅ timer.c — 비교 함수 구현

sleep_list 오름차순 정렬을 위한 비교 함수:

```c
static bool
wakeup_tick_less (const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux UNUSED)
{
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);

    return ta->wakeup_tick < tb->wakeup_tick;
}
```

- `list_entry(elem조각, struct thread, elem)` → elem 조각에서 struct thread 전체로 거슬러 올라감
- `ta->wakeup_tick < tb->wakeup_tick` → a가 더 작으면 true → 오름차순 정렬
- `UNUSED` → aux는 사용하지 않음

list_insert_ordered() 호출 형태:
```
list_insert_ordered(&sleep_list, &thread_current()->elem, wakeup_tick_less, NULL)
```

---

### ✅ timer.c — timer_sleep() 구현

busy-wait 제거, block 방식으로 전환:

```c
void
timer_sleep (int64_t ticks) {
    int64_t start = timer_ticks ();

    ASSERT (intr_get_level () == INTR_ON);

    if (ticks <= 0)           // ① 0 이하면 즉시 리턴
        return;

    thread_current()->wakeup_tick = start + ticks;  // ② 깨어날 시각 설정

    enum intr_level old_level = intr_disable();     // ③ 인터럽트 끄기

    list_insert_ordered(&sleep_list,                // ④ sleep_list 삽입
                        &thread_current()->elem,
                        wakeup_tick_less, NULL);

    thread_block();                                 // ⑤ 스레드 잠재우기

    intr_set_level(old_level);                      // ⑥ 인터럽트 복원
}
```

**핵심 포인트:**
- `ASSERT(intr_get_level() == INTR_ON)` — 인터럽트 꺼진 상태에서 호출하면 영원히 못 깨어남. 버그 방지용
- `old_level` 저장 이유 — 복원할 때 "원래 어떤 상태였는지" 알아야 하므로
- `intr_set_level(old_level)` — thread_block()에서 깨어난 뒤 인터럽트 복원

**삽질 포인트:**
- while 루프를 삭제하지 않고 주석 처리 → 반드시 **삭제**해야 함. 주석으로 남기면 코드 지저분해지고 혼동 유발

---

*마지막 업데이트: timer_sleep() 구현 완료*
