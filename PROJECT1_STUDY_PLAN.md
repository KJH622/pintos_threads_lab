# Pintos Project 1 팀 학습 계획

## 목표

이번 Project 1의 목표는 완성본 코드를 빠르게 만드는 것보다, 4명이 모두 Pintos의 thread scheduler를 이해하고 설명할 수 있게 되는 것이다.

우리는 아래 방식으로 진행한다.

```text
공동 범위 학습 + 개인 구현 + 매일 통합
```

즉, 매일 모두가 같은 범위를 공부하고 각자 구현한다. 하루 끝에는 각자 구현 방식과 이해한 내용을 공유한 뒤, 가장 이해하기 쉽고 안정적인 방식을 채택해서 메인 브랜치에 반영한다.

중요한 원칙은 하나다.

```text
오늘 구현한 코드를 4명 모두가 설명하지 못하면 아직 끝난 것이 아니다.
```

## 진행 원칙

1. 매일 같은 범위를 공부한다.
2. 각자 자기 브랜치에서 직접 구현한다.
3. 하루 끝에 각자 구현 내용을 발표한다.
4. 테스트 결과뿐 아니라 이해 가능성, 안정성, 문서 요구사항 일치 여부를 기준으로 채택한다.
5. 채택된 구현만 메인 브랜치에 반영한다.
6. 마지막 날 한 번에 합치지 않고, 매일 통합한다.

## 브랜치 운영

`main` 브랜치는 채택된 구현만 올라가는 브랜치로 사용한다.

각자는 매일 자기 브랜치를 따로 만든다.

```bash
git checkout -b 이름/day1-alarm
git checkout -b 이름/day2-priority
git checkout -b 이름/day3-donation
git checkout -b 이름/day4-mlfqs
```

예시:

```bash
git checkout -b jihyun/day1-alarm
git checkout -b donghyun/day1-alarm
```

메인 브랜치에 바로 push하지 않는다. 하루 끝 공유 후 채택된 코드만 메인에 반영한다.

## 채택 기준

채택 기준은 단순히 테스트를 많이 통과하는 코드가 아니다.

아래 기준을 함께 본다.

- Pintos 공식 문서 요구사항과 맞는가?
- 4명 모두 설명할 수 있는가?
- 구현이 단순하고 읽기 쉬운가?
- interrupt disable 범위가 너무 길지 않은가?
- busy waiting이 없는가?
- 다음 기능과 충돌이 적은가?
- 테스트가 안정적으로 통과하는가?

## 매일 발표 형식

각자 5분 정도로 아래 내용을 발표한다.

```text
1. 내가 바꾼 파일
2. 핵심 자료구조
3. thread가 어떤 흐름으로 움직이는지
4. 통과한 테스트
5. 내 방식의 장점
6. 내 방식의 약점
```

발표 후에는 팀원들이 질문한다. 질문에 답하지 못한 부분은 그날 정리해서 다시 공유한다.

## 0.5일차: 오늘 저녁

### 목표

구현보다 Pintos thread 흐름을 이해한다.

오늘은 코드를 많이 짜기보다, 각자 흐름도를 만들어오는 것이 좋다.

### 읽을 파일

```text
pintos/threads/thread.c
pintos/include/threads/thread.h
pintos/threads/synch.c
pintos/include/threads/synch.h
pintos/devices/timer.c
pintos/include/lib/kernel/list.h
```

### 확인할 함수

```text
thread_create()
thread_unblock()
thread_yield()
thread_block()
next_thread_to_run()
schedule()
timer_sleep()
timer_interrupt()
sema_down()
sema_up()
lock_acquire()
lock_release()
```

### 공유 질문

```text
thread는 어떤 상태들을 거치는가?
ready_list에는 언제 들어가고 언제 빠지는가?
timer interrupt는 thread 실행 흐름에 어떤 영향을 주는가?
semaphore와 lock은 내부적으로 thread_block을 어떻게 사용하는가?
list_entry()는 왜 필요한가?
```

### 하루 끝 산출물

```text
thread_create -> thread_unblock -> ready_list -> schedule -> next_thread_to_run
```

위 흐름을 각자 말로 설명할 수 있어야 한다.

## Day 1: Alarm Clock

### 목표

busy waiting 없는 `timer_sleep()`을 구현한다.

### 구현 범위

```text
timer_sleep() busy waiting 제거
sleep list 추가
struct thread에 wakeup_tick 추가
timer_sleep()에서 현재 thread block
timer_interrupt()에서 깨어날 thread unblock
```

### 핵심 개념

```text
busy waiting 금지
timer interrupt에서는 lock 사용 불가
interrupt disable은 짧게 사용
잠든 thread는 ready_list가 아니라 sleep list에 있어야 함
```

### 비교 기준

```text
sleep list를 정렬했는가?
timer_interrupt에서 매번 불필요하게 전체 list를 도는가?
alarm-zero, alarm-negative를 자연스럽게 처리하는가?
interrupt disable 범위가 너무 길지 않은가?
```

### 테스트

```bash
cd pintos/threads
make tests/threads/alarm-single.result
make tests/threads/alarm-multiple.result
make tests/threads/alarm-simultaneous.result
make tests/threads/alarm-priority.result
make tests/threads/alarm-zero.result
make tests/threads/alarm-negative.result
```

### 하루 끝 공유 질문

```text
왜 while + thread_yield 방식이 busy waiting인가?
잠든 thread는 어떤 경로로 다시 ready 상태가 되는가?
timer_interrupt 안에서 왜 lock을 쓰면 안 되는가?
```

## Day 2: Priority Scheduling

### 목표

priority donation 없이, 높은 priority thread가 먼저 실행되도록 만든다.

이날은 donation을 섞지 않는다.

### 구현 범위

```text
ready_list priority 정렬
thread_unblock()에서 높은 priority thread 선점
thread_yield()에서도 priority 순서 유지
sema_up()에서 highest priority waiter 깨우기
cond_signal()에서 highest priority waiter 깨우기
thread_set_priority() 후 필요하면 yield
```

### 핵심 개념

```text
PRI_MIN = 0
PRI_DEFAULT = 31
PRI_MAX = 63
숫자가 클수록 높은 priority
ready_list뿐 아니라 semaphore/condition waiters도 priority를 고려해야 함
```

### 테스트

```bash
cd pintos/threads
make tests/threads/priority-change.result
make tests/threads/priority-preempt.result
make tests/threads/priority-fifo.result
make tests/threads/priority-sema.result
make tests/threads/priority-condvar.result
```

### 하루 끝 공유 질문

```text
왜 ready_list만 정렬하면 부족한가?
semaphore waiters도 왜 priority 순이어야 하는가?
condition variable의 waiter는 어떤 구조로 되어 있는가?
thread_set_priority() 후 언제 yield해야 하는가?
```

## Day 3: Priority Donation

### 목표

lock 때문에 생기는 priority inversion을 이해하고 priority donation을 구현한다.

Project 1에서 가장 중요한 고비다.

### 구현 범위

```text
base_priority와 effective priority 분리
thread가 기다리는 lock 저장
donations list 추가
lock_acquire()에서 donation 전파
lock_release()에서 해당 lock 관련 donation 회수
multiple donation 처리
nested donation 처리
thread_get_priority()는 donation이 반영된 priority 반환
```

### 핵심 개념

```text
priority inversion
priority donation
multiple donation
nested donation
donation은 lock에 대해서만 필수
semaphore/condition에는 priority scheduling은 적용해야 함
```

### 테스트

```bash
cd pintos/threads
make tests/threads/priority-donate-one.result
make tests/threads/priority-donate-multiple.result
make tests/threads/priority-donate-multiple2.result
make tests/threads/priority-donate-nest.result
make tests/threads/priority-donate-chain.result
make tests/threads/priority-donate-sema.result
make tests/threads/priority-donate-lower.result
```

### 하루 끝 공유 질문

```text
lock을 기다리는 순간 누구에게 donation하는가?
lock_release() 때 어떤 donation만 제거해야 하는가?
multiple donation에서 가장 높은 priority는 어떻게 유지하는가?
nested donation은 어떤 순서로 전파되는가?
priority-donate-chain 테스트는 어떤 상황을 검사하는가?
```

## Day 4: MLFQS와 전체 정리

### 목표

`-mlfqs` 옵션에서 동작하는 4.4BSD-style scheduler를 구현하고 전체 테스트를 정리한다.

### 구현 범위

```text
fixed-point 연산 구현
nice 추가
recent_cpu 추가
load_avg 추가
매 tick마다 running thread의 recent_cpu 증가
매 1초마다 load_avg와 모든 thread의 recent_cpu 갱신
매 4 tick마다 priority 갱신
-mlfqs 모드에서는 donation 비활성화
```

### 공식

```text
priority = PRI_MAX - recent_cpu / 4 - nice * 2
recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
load_avg = (59/60) * load_avg + (1/60) * ready_threads
```

### 핵심 개념

```text
kernel에서는 floating point 사용 금지
fixed-point 정수 연산 필요
recent_cpu는 음수가 될 수 있으므로 0으로 clamp하면 안 됨
load_avg와 recent_cpu는 정확히 TIMER_FREQ마다 갱신
priority는 4 tick마다 갱신
MLFQS에서는 thread_set_priority()를 사실상 무시
```

### 테스트

```bash
cd pintos/threads
make tests/threads/mlfqs/mlfqs-load-1.result
make tests/threads/mlfqs/mlfqs-load-60.result
make tests/threads/mlfqs/mlfqs-load-avg.result
make tests/threads/mlfqs/mlfqs-recent-1.result
make tests/threads/mlfqs/mlfqs-fair-2.result
make tests/threads/mlfqs/mlfqs-fair-20.result
make tests/threads/mlfqs/mlfqs-nice-2.result
make tests/threads/mlfqs/mlfqs-nice-10.result
make tests/threads/mlfqs/mlfqs-block.result
```

### 전체 테스트

```bash
cd pintos/threads
make check
```

### 하루 끝 공유 질문

```text
priority scheduling과 MLFQS는 무엇이 다른가?
왜 kernel에서 float을 쓰지 않는가?
ready_threads는 정확히 무엇을 세야 하는가?
왜 load_avg와 recent_cpu 갱신 시점이 중요할까?
MLFQS에서 donation을 끄지 않으면 어떤 문제가 생기는가?
```

## 최종 점검

마지막에는 아래를 확인한다.

```bash
cd pintos/threads
make clean
make check
```

그리고 각자 아래 질문에 답할 수 있어야 한다.

```text
thread가 생성되어 실행되기까지의 흐름은?
timer_sleep()은 어떻게 busy waiting을 피하는가?
ready_list는 왜 priority 순서여야 하는가?
sema_up()과 cond_signal()은 왜 highest priority를 깨워야 하는가?
priority donation은 언제 시작되고 언제 끝나는가?
nested donation은 어떻게 전파되는가?
MLFQS에서 priority는 어떤 공식으로 계산되는가?
fixed-point는 왜 필요한가?
```

## 참고 문서

- KAIST Pintos Project 1 Introduction: https://casys-kaist.github.io/pintos-kaist/project1/introduction.html
- Alarm Clock: https://casys-kaist.github.io/pintos-kaist/project1/alarm_clock.html
- Priority Scheduling: https://casys-kaist.github.io/pintos-kaist/project1/priority_scheduling.html
- Advanced Scheduler: https://casys-kaist.github.io/pintos-kaist/project1/advanced_scheduler.html
- Threads Appendix: https://casys-kaist.github.io/pintos-kaist/appendix/threads.html
- Synchronization Appendix: https://casys-kaist.github.io/pintos-kaist/appendix/synchronization.html
