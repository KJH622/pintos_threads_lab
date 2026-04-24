# CLAUDE.md

이 파일은 Claude Code(`claude.ai/code`)가 이 저장소에서 작업할 때 반드시 따라야 할 작업 기준을 정의합니다.

## 절대 규칙

- 모든 개발과 테스트는 반드시 **Docker DevContainer**(Ubuntu 22.04 x86-64) 내부에서만 진행합니다.
- 호스트 환경에서 직접 빌드하거나 테스트하지 않습니다.
- `pintos` 관련 명령을 실행하기 전에는 반드시 `source pintos/activate`를 수행합니다.
- 생성 산출물(`os.dsk`, `kernel.o`, `*.tar.gz` 등)은 특별한 이유가 없으면 수정 대상에 포함하지 않습니다.
- LF 줄바꿈 규칙을 유지하며, 저장소의 `.gitattributes` 설정을 거스르지 않습니다.
- 커밋 메시지는 반드시 **한국어 + Angular Commit Convention** 형식을 사용합니다.
- 사용자 요청 없이 위험한 Git 명령(`reset --hard`, 강제 push, 무단 브랜치 정리)을 실행하지 않습니다.
- `vm/` 및 일부 `userprog/` 코드는 학생 구현용 스텁일 수 있으므로, 비어 있는 구현을 자동으로 버그로 단정하지 않습니다.

## 아키텍처

### 폴더 구조

```text
pintos/
├─ threads/      # 스레드, 스케줄러, 동기화, 인터럽트
├─ userprog/     # 사용자 프로세스, 예외 처리, 시스템 콜
├─ vm/           # 가상 메모리, 프레임, 스왑, 페이지 관리
├─ filesys/      # 파일시스템, 디렉터리, 캐시
├─ devices/      # 타이머, 디스크, 키보드, 시리얼, VGA
├─ lib/          # 공용 라이브러리와 자료구조
├─ include/      # 헤더 파일
├─ tests/        # 모듈별 테스트
└─ utils/        # pintos 실행 스크립트, 보조 도구
```

### 기술 스택

```text
언어
├─ C
└─ x86-64 Assembly

실행 환경
├─ Ubuntu 22.04 DevContainer
├─ QEMU
└─ x86-64 long mode

빌드/디버깅 도구
├─ make
├─ GCC
├─ GDB
└─ Python 기반 pintos 유틸리티
```

### 핵심 파일

- `threads/thread.h`: `struct thread`, 스케줄러 API, 우선순위, MLFQS 정의
- `threads/synch.h`: `lock`, `semaphore`, `condition` 정의
- `devices/timer.c`: 타이머 인터럽트와 `timer_sleep()` 처리
- `userprog/process.h`: 프로세스 생성/실행/종료 인터페이스
- `userprog/syscall.c`: 시스템 콜 디스패처
- `vm/vm.h`: 보조 페이지 테이블, 페이지 타입, 프레임 관리 API
- `filesys/filesys.h`: 파일시스템 초기화/생성/열기 API

## 빌드/테스트

### 환경 활성화

```bash
source pintos/activate
```

### 빌드

```bash
cd pintos/threads    # 또는 userprog, vm, filesys
make
make clean
```

### 테스트

```bash
cd pintos/threads
make check
pintos -- run <test-name>
```

### 제출 아카이브 생성

```bash
cd pintos
make TEAM=<team-number> archive
```

### 작업 원칙

- 테스트는 수정한 모듈 디렉터리에서 실행합니다.
- 단일 기능 수정 시에는 먼저 개별 테스트를 실행하고, 이후 `make check`로 범위를 넓힙니다.
- 배포 개념은 일반 서비스 배포가 아니라 **제출용 아카이브 생성**으로 간주합니다.

## 도메인 컨텍스트

### 핵심 용어

- `thread`: 커널 스케줄링의 기본 단위
- `ready_list`: 실행 준비가 된 스레드 목록
- `blocked`: 이벤트를 기다리며 잠든 상태
- `semaphore`, `lock`, `condition`: 동기화 프리미티브
- `priority donation`: 우선순위 역전 방지를 위한 기부 메커니즘
- `MLFQS`: 고급 스케줄러, `nice`, `recent_cpu`, `load_avg` 기반 우선순위 계산
- `SPT`: Supplemental Page Table, 가상 메모리 매핑 추적 구조
- `frame`: 실제 물리 메모리 페이지
- `swap`: 프레임 부족 시 페이지를 디스크로 내보내는 영역

### 데이터 흐름

```text
타이머 인터럽트 발생
→ timer_interrupt()
→ thread_tick()
→ 현재 스레드 상태/타임슬라이스 갱신
→ 필요 시 선점 또는 sleep 스레드 깨우기
→ scheduler가 다음 실행 스레드 선택
```

```text
사용자 프로그램 실행
→ process_create_initd()/fork()/exec()
→ 시스템 콜 또는 예외 처리 진입
→ 필요한 메모리/파일시스템 자원 접근
→ wait()/exit()를 통해 부모-자식 관계 정리
```

```text
가상 메모리 접근
→ 페이지 폴트
→ SPT 조회
→ 프레임 확보 또는 swap-in
→ 페이지 매핑 복구
→ 실행 재개
```

### 프로젝트 맥락

- 이 저장소는 KAIST Pintos 계열 과제를 위한 교육용 운영체제 코드베이스입니다.
- 각 프로젝트는 이전 프로젝트 위에 누적되므로, `threads`의 품질이 이후 `userprog`, `vm`, `filesys` 안정성에 직접 영향을 줍니다.
- 테스트 통과 자체가 중요하지만, 이후 프로젝트에 이어질 수 있는 구조적 일관성도 함께 고려합니다.

## 코딩 컨벤션

### 네이밍 규칙

- 기존 Pintos 스타일을 따릅니다.
- 함수명은 `thread_unblock`, `lock_acquire`처럼 `snake_case`를 사용합니다.
- 매크로와 상수는 `PRI_MAX`, `TID_ERROR`처럼 대문자 스네이크 케이스를 사용합니다.
- 구조체 필드명은 짧고 의미 있게 유지하고, 기존 이름을 불필요하게 바꾸지 않습니다.

### 커밋 규칙

- 커밋 메시지는 반드시 한국어로 작성합니다.
- 형식은 반드시 Angular Commit Convention을 사용합니다.

```text
type(scope): 변경 내용
```

- 예시:

```text
feat(threads): alarm clock 구현
fix(synch): priority donation 복원 로직 수정
docs(claude): CLAUDE.md 작업 가이드 문서 추가
chore(git): .gitattributes 추가 및 LF 줄바꿈 규칙 설정
```

### 구현 패턴 규칙

- 기존 모듈의 구현 방식과 스타일을 우선 존중합니다.
- 작은 함수 분리는 좋지만, 운영체제 흐름을 해칠 정도로 과도한 추상화는 피합니다.
- 인터럽트 비활성화 구간은 최소화하고, 필요한 범위에서만 사용합니다.
- 동기화 로직은 테스트 케이스 기대 동작과 함께 검증합니다.
- 스텁 코드를 구현할 때는 테스트 통과뿐 아니라 이후 프로젝트와의 연결성도 고려합니다.
- 디버깅용 출력은 필요 시만 추가하고, 최종 제출 전에는 제거 여부를 점검합니다.

## 참고 문서

- 공식 문서: https://casys-kaist.github.io/pintos-kaist/
