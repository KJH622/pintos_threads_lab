# Pintos Project 1: Threads — 흐름 파악용 정리

> 출처: https://casys-kaist.github.io/pintos-kaist/project1/introduction.html
> 목적: **내가 어떤 것을, 왜, 어떤 순서로 만들어야 하는지** 흐름을 잡기 위한 문서

---

## 🎯 이 프로젝트가 뭐냐?

**Pintos**는 KAIST에서 교육용으로 쓰는 작은 OS 커널이다.
이미 **최소한으로 돌아가는 스레드 시스템**이 구현돼 있고, 나는 이걸 **확장**해서
"동기화(synchronization) 문제를 제대로 이해하는 것"이 이 프로젝트의 진짜 목표다.

- **작업 디렉터리**: 주로 `threads/`, 일부 `devices/`
- **컴파일**: `threads/` 디렉터리에서 수행
- **선행 학습**: 시작 전 [Synchronization appendix](https://casys-kaist.github.io/pintos-kaist/appendix/synchronization.html) 훑어보기

---

## 📚 알아둬야 할 배경 지식

### 1. 스레드(Thread)란?

- 스레드를 만든다 = **새로운 실행 컨텍스트(context)를 스케줄링 대상으로 등록**한다
- `thread_create()`에 함수 포인터를 넘김 → 스레드가 실행될 때 그 함수가 `main()`처럼 동작
- **함수가 return하면 스레드 종료**
- 어느 시점이든 **CPU는 딱 한 스레드만 실행**, 나머지는 대기
- 실행할 스레드가 없으면 `idle()` 스레드가 돌아감
- 동기화 프리미티브(세마포어, 락 등)가 컨텍스트 스위치를 강제할 수 있음

> 컨텍스트 스위치 메커니즘은 `thread_launch()` (`threads/thread.c`)에 구현되어 있음.
> 이해할 필요는 없지만, GDB로 `schedule()`에 breakpoint 걸고 한번 따라가보면 큰 도움이 됨.

### 2. ⚠️ 경고 — 스택 크기 제한

- 각 스레드의 실행 스택은 **4KB 미만**
- 따라서 이런 코드는 **스택 오버플로우**로 커널 패닉을 일으킬 수 있음:
  ```c
  int buf[1000];  // ❌ 지역변수로 이렇게 큰 배열 선언 금지
  ```
- 큰 데이터가 필요하면 **페이지 할당자(palloc)** 또는 **블록 할당자(malloc)** 사용

### 3. 동기화(Synchronization) 원칙

- **"인터럽트 꺼버리면 되잖아?" → 금지.** 모든 문제를 이렇게 풀면 안 됨
- **대신 세마포어, 락, 조건변수를 써라**
- 인터럽트 끄기는 **오직 이 경우에만**: 커널 스레드와 인터럽트 핸들러가 데이터를 공유할 때
  - 이유: 인터럽트 핸들러는 sleep 불가 → 락을 못 잡음 → 어쩔 수 없이 인터럽트 off로 보호
- 이번 프로젝트에서 인터럽트 off가 필요한 곳:
  - **Alarm Clock**: 타이머 인터럽트가 자는 스레드를 깨울 때
  - **Advanced Scheduler**: 타이머 인터럽트가 전역/스레드 변수 접근할 때
- 인터럽트 끌 때는 **최소 범위**만. 너무 오래 끄면 타이머 틱이나 입력 이벤트를 놓침
- **Busy waiting 금지** — `thread_yield()`를 반복 호출하는 tight loop도 busy waiting임

### 4. 개발 팁

- 팀원끼리 조각내서 따로 작업하다 마감 직전에 합치면 **높은 확률로 망함**
- **git으로 자주 통합(integrate early and often)**

---

## 🗂️ 소스 파일 로드맵 — 어디를 봐야 하나?

### `threads/` 디렉터리

| 파일 | 역할 | 수정 여부 |
|---|---|---|
| `thread.c`, `thread.h` | **핵심.** `struct thread` 정의, 생성/종료, 스케줄러 | ✅ **많이 수정** |
| `synch.c`, `synch.h` | 세마포어, 락, 조건변수, 최적화 배리어 | ✅ 수정 |
| `init.c`, `init.h` | 커널 초기화, `main()` | 👀 살펴보고 필요하면 수정 |
| `palloc.c`, `palloc.h` | 페이지 할당자 (4KB 단위) | 사용만 |
| `malloc.c`, `malloc.h` | 커널용 `malloc`/`free` | 사용만 |
| `interrupt.c`, `interrupt.h` | 인터럽트 on/off, 핸들링 | 사용만 |
| `loader.S`, `kernel.lds.S`, `start.S` | 로더, 링커 스크립트, 부팅 | ❌ 건드리지 말 것 |
| `intr-stubs.S` | 인터럽트 처리 저수준 어셈블리 | ❌ |
| `mmu.c`, `mmu.h` | x86-64 페이지 테이블 | Project 3에서 봄 |
| `vaddr.h`, `pte.h`, `flags.h` | 가상주소/페이지 테이블 엔트리/플래그 매크로 | Project 1에선 무시 |

### `devices/` 디렉터리

| 파일 | 역할 | 수정 여부 |
|---|---|---|
| `timer.c`, `timer.h` | **시스템 타이머 (기본 100Hz)** | ✅ **수정 (Alarm Clock)** |
| `vga.c`, `serial.c` | 화면/시리얼 출력 (printf가 자동 호출) | ❌ |
| `kbd.c`, `input.c` | 키보드 & 입력 레이어 | ❌ |
| `block.c`, `ide.c`, `partition.c` | 블록 디바이스 (Project 2부터 사용) | ❌ |
| `intq.c` | 인터럽트 큐 | ❌ |
| `rtc.c`, `speaker.c`, `pit.c` | RTC, 스피커, 8254 타이머 | ❌ |

### `lib/` & `lib/kernel/` 디렉터리

| 파일 | 역할 |
|---|---|
| `kernel/list.c`, `list.h` | **이중 연결 리스트 — 프로젝트 내내 엄청 씀. 주석 꼭 읽을 것!** |
| `kernel/bitmap.c` | 비트맵 (Project 1에선 거의 안 씀) |
| `kernel/hash.c` | 해시 테이블 (Project 3에서 유용) |
| `kernel/console.c` | `printf()` 구현 |
| `debug.c`, `debug.h` | 디버깅 매크로 |
| `random.c` | 의사난수 생성기 |
| 표준 C 라이브러리 일부 | `stdio`, `stdlib`, `string` 등 |

---

## 🎯 실제로 해야 할 일 — 3단계 서브과제

Project 1은 3개의 서브과제로 나뉜다. **쉬운 것부터 순서대로** 진행.

```
[Step 1] Alarm Clock         ★        ← 여기부터 시작
[Step 2] Priority Scheduling ★★
[Step 3] Advanced Scheduler  ★★★      ← MLFQS, 가장 어려움
```

---

### 🔹 STEP 0: 환경 세팅 & 코드 탐색 (1~2일)

**목표**: 빌드 성공시키고 코드 구조 파악

- [ ] "Getting Started" 페이지 따라 Pintos 빌드 & QEMU에서 실행
- [ ] `threads/thread.h`의 `struct thread` 구조 이해 — 이게 **TCB(Thread Control Block)** 역할
- [ ] `threads/thread.c`의 주요 함수 읽기:
  - `thread_create()` — 스레드 생성
  - `schedule()` — 스케줄링 핵심
  - `thread_yield()` — CPU 양보
  - `thread_block()` / `thread_unblock()` — 블로킹/언블로킹
- [ ] `threads/synch.c`의 세마포어/락/조건변수 구현 읽기
- [ ] **`lib/kernel/list.h` 주석 정독** — 앞으로 이 리스트 API를 수없이 사용함
- [ ] GDB로 `schedule()`에 breakpoint 걸고 컨텍스트 스위치 한 번 따라가보기

---

### 🔹 STEP 1: Alarm Clock (★)

**문제 상황**: 현재 `timer_sleep()`은 **busy waiting**으로 구현되어 있음
→ while 돌면서 `thread_yield()`를 계속 호출 → CPU 낭비

**왜 만들어야 하나?**
- Busy waiting은 금지된 패턴
- 자는 동안은 CPU를 완전히 안 쓰도록 **블로킹(sleep) 방식**으로 바꿔야 함

**만들어야 할 것**:
1. 잠들어야 하는 스레드를 "sleep list"에 추가
2. `thread_block()`으로 스레드를 블록 상태로
3. 매 타이머 틱마다(`timer_interrupt`) 깨어날 시간이 된 스레드를 찾아서 `thread_unblock()`

**수정 파일**: `devices/timer.c`, `threads/thread.c`, `threads/thread.h`

---

### 🔹 STEP 2: Priority Scheduling (★★)

**문제 상황**: 현재 스케줄러는 라운드로빈 기반이라 **우선순위를 무시**함

**왜 만들어야 하나?**
- 실제 OS는 중요한 일(높은 우선순위)을 먼저 실행해야 함
- 그런데 단순히 우선순위만 보면 **우선순위 역전(priority inversion)** 문제 발생
  → 이걸 **Priority Donation(우선순위 기부)**으로 해결

**만들어야 할 것**:
1. **항상 우선순위가 가장 높은 스레드가 CPU를 차지**하도록 변경
2. 락/세마포어/조건변수의 **대기 큐도 우선순위순 정렬**
3. **Priority Donation 구현**:
   - 낮은 우선순위 스레드 L이 락을 잡고 있는데, 높은 우선순위 H가 그 락을 기다리면?
   - H가 자신의 우선순위를 L에게 "기부"해서 L이 빨리 끝내고 락을 풀도록
4. Nested donation(연쇄 기부)과 multiple donation(여러 명에게 받기)도 처리

**수정 파일**: `threads/thread.c`, `threads/synch.c`

---

### 🔹 STEP 3: Advanced Scheduler / MLFQS (★★★)

**문제 상황**: 위의 priority scheduling은 **정적 우선순위** → 유연하지 못함

**왜 만들어야 하나?**
- 4.4BSD 스타일의 **MLFQS (Multi-Level Feedback Queue Scheduler)** 구현
- 스레드의 CPU 사용량에 따라 **우선순위를 동적으로 조정**
  → CPU 많이 쓴 스레드는 우선순위 내림, 적게 쓴 건 올림 (공정성 확보)

**만들어야 할 것**:
1. **고정소수점(fixed-point) 실수 연산 구현**
   → 커널에는 부동소수점 유닛(FPU)이 없어서, 정수로 소수 표현해야 함
2. 세 가지 값 계산:
   - `recent_cpu`: 최근 CPU 사용량 (스레드별)
   - `load_avg`: 시스템 평균 부하 (전역)
   - `nice`: 친절도(사용자 지정)
3. 공식에 따라 **매 틱마다 / 매 초마다 재계산**하고 우선순위 갱신

**수정 파일**: `threads/thread.c` (주로)

---

## ✅ 지금 당장 할 일 (TODO)

1. [ ] **"Getting Started" 페이지 보고 빌드 환경 먼저 세팅** — 빌드 안 되면 아무것도 못 함
2. [ ] **"Synchronization" appendix** 훑어보기 — 세마포어/락 개념 잡기
3. [ ] `threads/thread.c`, `threads/thread.h` 정독
4. [ ] `lib/kernel/list.h` 주석 읽기
5. [ ] **Alarm Clock 페이지**로 넘어가서 Step 1 시작

---

## 💡 기억할 원칙

| 원칙 | 설명 |
|---|---|
| **Busy waiting 금지** | `thread_yield()` loop도 포함 |
| **큰 지역변수 금지** | 스택 4KB 제한 → `malloc`/`palloc` 사용 |
| **인터럽트 off는 최후의 수단** | 세마포어/락/조건변수를 우선 사용 |
| **인터럽트 off는 최소 범위로** | 길어지면 타이머·입력 놓침 |
| **디버깅 코드는 제출 전 삭제** | 주석 처리만 하면 가독성 나빠짐 |
| **git으로 자주 통합** | 마감 직전 merge 지옥 방지 |