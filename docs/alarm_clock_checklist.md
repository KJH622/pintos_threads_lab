# Day 0.5 + Day 1 — Alarm Clock 체크리스트

> **Day 0.5** (4/24 금 저녁) + **Day 1** (4/25 토 오늘): Alarm Clock 구현 완료  
> 수정 파일: `threads/thread.h`, `threads/thread.c`, `devices/timer.c`

---

## 통과해야 할 테스트 (5개)

- [ ] `alarm-zero` — `timer_sleep(0)` 호출 시 즉시 리턴
- [ ] `alarm-negative` — `timer_sleep(음수)` 호출 시 즉시 리턴
- [ ] `alarm-single` — 스레드 1개가 정확한 tick에 깨어남
- [ ] `alarm-multiple` — 스레드 여러 개가 각자 다른 tick에 깨어남
- [ ] `alarm-simultaneous` — 스레드 여러 개가 동일 tick에 동시에 깨어남

---

## 구현 체크리스트

### Step 1 — `threads/thread.h` : struct thread 필드 추가

- [x] `int64_t wakeup_tick` 추가 (Alarm Clock 핵심)
- [ ] *(선행 투자)* `int original_priority` 선언
- [ ] *(선행 투자)* `struct lock *wait_on_lock` 선언
- [ ] *(선행 투자)* `struct list donations` 선언
- [ ] *(선행 투자)* `struct list_elem donation_elem` 선언
- [ ] *(선행 투자)* `int nice` 선언
- [ ] *(선행 투자)* `int recent_cpu` 선언

> **왜?**  
> `timer_interrupt()`가 매 tick마다 "이 스레드 지금 깨울 시간 됐나?"를 판단하려면, 스레드 자신이 "나는 몇 번 tick에 깨어나야 해"라는 정보를 직접 들고 있어야 한다. 그 정보를 담는 그릇이 `wakeup_tick`이다.  
> 선행 투자 필드들은 Alarm Clock과 직접 관련은 없지만, Priority Donation / MLFQS 구현 때 `thread.h`를 다시 열어 수정하는 번거로움을 없애기 위해 지금 같이 선언해둔다.

---

### Step 2 — `threads/thread.c` : sleep_list 초기화

- [x] 파일 상단에 `static struct list sleep_list` 전역 선언
- [x] `thread_init()` 안에 `list_init(&sleep_list)` 추가

> **왜?**  
> 잠든 스레드들을 모아둘 전용 공간이 필요하다. `ready_list`는 "실행 준비된 스레드" 목록이므로 여기에 섞으면 안 된다. 별도의 `sleep_list`를 만들어야 `timer_interrupt()`가 잠든 스레드만 골라서 순회할 수 있다.  
> `thread_init()`에서 초기화하는 이유는, PintOS 부팅 시 스레드 시스템이 초기화되는 시점이 거기이기 때문이다. 그 전에 리스트를 쓰려 하면 초기화 안 된 메모리를 건드리게 된다.

---

### Step 3 — `threads/thread.c` : `thread_sleep()` 신규 작성

- [ ] 인터럽트 비활성화 (`intr_disable`)
- [ ] 현재 스레드 `wakeup_tick` 설정
- [ ] `sleep_list`에 `wakeup_tick` 오름차순 정렬 삽입 (`list_insert_ordered`)
- [ ] `thread_block()` 호출
- [ ] 인터럽트 복원 (`intr_set_level`)

> **왜?**  
> `thread_block()`은 인터럽트가 비활성화된 상태에서만 안전하게 호출할 수 있다. 인터럽트가 켜진 채로 block을 시도하면, 그 사이에 `timer_interrupt()`가 끼어들어 경쟁 상태가 발생할 수 있다. 따라서 인터럽트 끄기 → `wakeup_tick` 설정 → `sleep_list` 삽입 → `thread_block()` → 인터럽트 복원을 한 묶음으로 처리하는 함수가 필요하다.  
> `sleep_list`에 오름차순 정렬 삽입을 하는 이유는, `timer_interrupt()`에서 맨 앞 원소의 `wakeup_tick`이 아직 안 됐으면 나머지는 볼 필요도 없이 바로 `break`할 수 있어서 매 tick마다의 순회 비용이 크게 줄어들기 때문이다.

---

### Step 4 — `devices/timer.c` : `timer_sleep()` 수정

- [ ] `ticks <= 0` 이면 즉시 리턴 → `alarm-zero`, `alarm-negative` 통과 조건
- [ ] 기존 busy-wait 루프 전부 제거
- [ ] 인터럽트 비활성화 후 `thread_sleep()` 호출

> **왜?**  
> 기존 코드는 "아직 시간 안 됐나? 아직? 아직?" 하며 CPU를 계속 점유하는 busy-wait 방식이다. 이를 제거하고 `thread_sleep()`을 호출해 스레드를 sleep 상태로 전환해야 CPU를 낭비하지 않는다.  
> `ticks <= 0` 가드는 0이나 음수를 넘기면 재울 필요가 없기 때문에 즉시 리턴하는 처리다. `alarm-zero`와 `alarm-negative` 테스트가 이 케이스를 검증한다.

---

### Step 5 — `devices/timer.c` : `timer_interrupt()` 수정

- [ ] `sleep_list` 앞에서부터 순회
- [ ] `wakeup_tick <= timer_ticks()` 인 스레드 → 리스트 제거 후 `thread_unblock()`
- [ ] 첫 원소의 `wakeup_tick > timer_ticks()` 이면 `break` (정렬 덕분에 가능)

> **왜?**  
> `timer_interrupt()`는 하드웨어가 매 tick마다 자동으로 호출하는 함수다. 즉, 잠든 스레드를 깨워줄 수 있는 유일한 주체다. Step 3에서 `sleep_list`에 스레드를 넣는 구조를 만들었으니, 여기서 꺼내는(깨우는) 처리를 추가해야 전체 흐름이 완성된다.  
> Step 3에서 오름차순 정렬 삽입을 한 덕분에, 맨 앞 원소가 아직 깨울 시간이 아니면 뒤도 전부 아직이므로 즉시 `break`할 수 있다.

---

## 주의사항

- 인터럽트 컨텍스트(`timer_interrupt()`)에서 `thread_block()` 직접 호출 절대 금지
- `sleep_list`는 반드시 오름차순 정렬 삽입 유지 → `timer_interrupt()` 순회 비용 감소
- `thread_sleep()`의 헤더 선언을 `thread.h`에 추가해야 `timer.c`에서 호출 가능
