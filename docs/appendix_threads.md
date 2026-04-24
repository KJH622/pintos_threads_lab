# Pintos Appendix: Threads — 흐름 파악용 정리

> 출처: https://casys-kaist.github.io/pintos-kaist/appendix/threads.html
> 목적: Project 1 시작 전에 반드시 이해해야 하는 **`struct thread` 자료구조 + 핵심 함수 레퍼런스**
> 이 페이지는 "구현하라"가 아니라 **"읽고 머릿속에 저장해둬라"** 문서다.

---

## 📖 이 페이지가 뭐냐?

Pintos 스레드가 **내부적으로 어떻게 생겼는지** 설명하는 레퍼런스.
크게 두 덩어리:
1. **`struct thread`** — 스레드의 모든 정보가 담긴 구조체
2. **`thread.c`의 주요 함수들** — 스레드 생성/종료/상태전이 API

---

## 🧱 ① `struct thread` — 스레드 구조체

> 위치: `threads/thread.h`
> 앞으로 내가 **멤버를 추가/수정/삭제하게 될 핵심 구조체**

### 💡 메모리 구조 (가장 중요!)

스레드 하나당 **4KB 페이지 한 장**을 독차지한다.

```
4 kB ┌─────────────────────────────┐
     │      커널 스택 (kernel stack)│  ← 스택은 위에서 아래로 자람
     │              ↓               │
     │        grows downward         │
     │                               │
     │                               │
     │                               │
sizeof(struct thread) ├──────────────┤
     │          magic                │  ← 스택 오버플로 감지용
     │          intr_frame           │
     │          status               │
     │          tid                  │
0 kB └─────────────────────────────┘  ← 이 자리에 struct thread
```

### ⚠️ 이 구조에서 나오는 두 가지 치명적 제약

1. **`struct thread`가 너무 커지면 안 됨**
   → 스택 공간이 부족해짐. **1KB 미만 유지 권장**
2. **커널 스택이 너무 커지면 안 됨**
   → 스택 오버플로우 나면 `struct thread`가 망가짐
   → **큰 지역변수 절대 금지**, `malloc()`이나 `palloc_get_page()` 사용

---

### 📋 멤버 변수 해설

| 멤버 | 의미 | 주의사항 |
|---|---|---|
| `tid_t tid` | 스레드 고유 ID (초기 스레드는 1부터) | `int` typedef |
| `enum thread_status status` | 스레드의 현재 상태 (4가지) | ⭐ |
| `char name[16]` | 스레드 이름 (디버깅용) | |
| `struct intr_frame tf` | **컨텍스트 스위치용** — 레지스터·스택 포인터 저장 | |
| `int priority` | 우선순위 0(PRI_MIN)~63(PRI_MAX), 숫자 클수록 높음 | ⭐ Priority Scheduling |
| `struct list_elem elem` | 이 스레드를 리스트에 넣을 때 쓰는 "고리" | ⭐ ready_list 또는 semaphore 대기 |
| `uint64_t *pml4` | 페이지 테이블 (Project 2부터) | Project 1에선 무시 |
| `unsigned magic` | `THREAD_MAGIC` 값 고정, **스택 오버플로우 감지용** | **멤버 추가 시 항상 맨 끝에 두기!** |

---

### 🚦 `thread_status` — 4가지 상태

| 상태 | 뜻 |
|---|---|
| `THREAD_RUNNING` | **지금 CPU를 쓰고 있는 스레드** (항상 정확히 1개) — `thread_current()`가 반환 |
| `THREAD_READY` | **실행 준비 완료**, 아직 CPU는 못 받음 — `ready_list`에 들어있음 |
| `THREAD_BLOCKED` | **뭔가를 기다리는 중** (락, 인터럽트 등) — `thread_unblock()` 전까지 스케줄 안 됨 |
| `THREAD_DYING` | **종료 예정** — 다음 스위치 때 스케줄러가 파괴 |

#### 상태 전이 흐름

```
     thread_create()
          │
          ▼
    [THREAD_BLOCKED]  ← 생성 직후 잠시
          │
          │ unblock
          ▼
    [THREAD_READY]  ⇄  [THREAD_RUNNING]
          ▲   block     │    │
          │             │    │ exit
          │ unblock     │    ▼
          │             │  [THREAD_DYING]
    [THREAD_BLOCKED] ◄──┘
```

---

### ⭐ `list_elem`의 트릭 (알면 이해가 쉬워짐)

- `list_elem`은 **하나만 있어도** `ready_list`와 세마포어 대기 리스트를 **동시에** 쓸 수 있음
- 왜? → **한 스레드가 "READY"이면서 동시에 "BLOCKED"일 수는 없으니까**
- 즉, 한 번에 한 리스트에만 속함 → 고리(elem) 하나로 충분

---

## 🔧 ② 주요 함수들 (`threads/thread.c`)

### 🔹 초기화/시작

| 함수 | 설명 |
|---|---|
| `thread_init()` | `main()`이 호출. Pintos의 **초기 스레드**용 `struct thread` 생성. 이게 끝나야 `thread_current()`가 정상 동작 |
| `thread_start()` | `main()`이 호출. **idle 스레드 생성** + 인터럽트 ON → 스케줄러 시작 |
| `thread_tick()` | **타이머 인터럽트가 매 틱마다 호출** — 통계 갱신, time slice 만료 시 스케줄러 트리거 |
| `thread_print_stats()` | 종료 시 통계 출력 |

### 🔹 생성/종료

| 함수 | 설명 |
|---|---|
| `thread_create(name, priority, func, aux)` | 새 스레드 생성. 페이지 할당 → 초기화 → BLOCKED로 시작 → 리턴 직전 UNBLOCK. 스레드는 `func(aux)`를 실행 |
| `thread_func(aux)` | `thread_create`에 넘기는 함수의 타입 |
| `thread_exit()` | 현재 스레드 종료 (절대 리턴 안 함) |

### 🔹 상태 전이 (⭐ Alarm Clock에서 핵심 사용)

| 함수 | 설명 |
|---|---|
| `thread_block()` | **현재 스레드 → BLOCKED**. "나 재워줘." 이후 `thread_unblock()` 없으면 영원히 안 깸. ⚠️ 저수준 함수 — 웬만하면 세마포어 쓰는 게 나음 |
| `thread_unblock(t)` | **스레드 t를 BLOCKED → READY**. 기다리던 이벤트 발생 시 호출 |
| `thread_yield()` | **현재 스레드가 CPU 양보** → 스케줄러가 다음 스레드 선택. (주의: 자기 자신이 다시 뽑힐 수도 있음) |

### 🔹 정보 조회

| 함수 | 설명 |
|---|---|
| `thread_current()` | 지금 실행 중인 스레드 반환 |
| `thread_tid()` | = `thread_current()->tid` |
| `thread_name()` | = `thread_current()->name` |

### 🔹 우선순위 (⭐ Priority Scheduling에서 구현)

| 함수 | 설명 |
|---|---|
| `thread_get_priority()` | 현재 스레드 우선순위 반환 (stub) |
| `thread_set_priority(new)` | 우선순위 설정 (stub) |

> **stub** = 껍데기만 있고 내용은 내가 채워야 함

### 🔹 Advanced Scheduler (⭐ MLFQS에서 구현)

| 함수 | 설명 |
|---|---|
| `thread_get_nice()` / `thread_set_nice()` | nice 값 get/set |
| `thread_get_recent_cpu()` | recent_cpu 반환 |
| `thread_get_load_avg()` | 시스템 load_avg 반환 |

---

## 🎯 그래서 내가 뭘 해야 하냐? — 흐름

이 페이지는 "구현하라"가 아니라 **"읽고 머릿속에 저장해둬라"** 문서.
다음 순서로 접근하면 된다.

### 📍 STEP 1: 지금 당장 할 일 (읽기만)

- [ ] `threads/thread.h` 열어서 **`struct thread` 실제 멤버 전부 확인** (이 문서 내용과 비교)
- [ ] 4개 상태 `RUNNING / READY / BLOCKED / DYING` **상태 전이도 머리에 그리기**
- [ ] `struct thread`와 커널 스택이 같은 4KB 페이지에 공존한다는 사실 이해
  → **큰 지역변수 금지** 각인

### 📍 STEP 2: 함수 맵핑 (머리에 넣기)

각 서브과제에서 쓸 함수들을 미리 매칭시켜두기:

```
┌─ Alarm Clock에서 쓸 것 ───────────────┐
│  thread_block()    ← 자러 갈 때        │
│  thread_unblock()  ← 깨울 때           │
│  thread_tick()     ← 매 틱마다 체크     │
└───────────────────────────────────────┘

┌─ Priority Scheduling에서 쓸 것 ──────┐
│  priority 멤버                        │
│  thread_get_priority/set_priority    │
│  thread_yield()  ← 높은 우선순위 양보  │
└──────────────────────────────────────┘

┌─ Advanced Scheduler에서 쓸 것 ───────┐
│  thread_get_nice/set_nice            │
│  thread_get_recent_cpu               │
│  thread_get_load_avg                 │
│  thread_tick()  ← 여기서 공식 계산    │
└──────────────────────────────────────┘
```

### 📍 STEP 3: 멤버 추가 원칙 (나중에 적용)

`struct thread`에 멤버를 추가하게 될 텐데, 그때 반드시 지킬 것:

1. **`magic`은 항상 맨 끝에 유지** (스택 오버플로 감지 기능 유지)
2. **구조체가 1KB 넘지 않게** (스택 공간 보존)
3. **큰 배열이나 버퍼는 포인터로** (직접 임베드 X, `malloc`으로 동적 할당)

---

## 🧩 예시: Alarm Clock 구현 시 함수 호출 흐름

이 페이지의 함수들을 어떻게 **조립**하는지 보여주는 예시:

```
[타이머 인터럽트 매 틱]
        ↓
    thread_tick()                   ← devices/timer.c에서 호출
        ↓
    (내가 추가할 로직)
    "이 틱에 깨어날 스레드 있나?"
        ↓
    있으면 → thread_unblock(t)      ← READY로 전환

[timer_sleep(ticks) 호출 시]
        ↓
    "일어날 시각 = now + ticks" 기록
        ↓
    sleep_list에 추가
        ↓
    thread_block()                  ← 나 재워줘
```

이 페이지에 나온 함수들을 이런 식으로 조립해서 쓰면 된다.

---

## 💡 기억할 핵심 3가지

| # | 원칙 |
|---|---|
| 1 | **스레드 하나 = 4KB 페이지 하나** (아래 struct thread + 위 커널 스택) |
| 2 | **상태 4가지**: RUNNING(1개뿐) / READY(대기줄) / BLOCKED(잠듦) / DYING(정리중) |
| 3 | **`magic`은 항상 맨 끝** — 스택 오버플로 감지를 지키는 장치 |