# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Environment

All development must happen **inside the Docker DevContainer** (Ubuntu 22.04 x86-64). Open in VSCode and use "Dev Containers: Reopen in Container". The container installs QEMU, GCC, GDB, and all required tooling.

Inside the container, the workspace is at `/workspaces/pintos_22.04_lab_docker/`. Source the environment before running pintos commands:

```bash
source pintos/activate
```

## Build

Each lab module is built independently from its own directory:

```bash
cd pintos/threads    # or userprog, vm, filesys
make                 # produces kernel.o, os.dsk, etc.
make clean
```

## Running Tests

Tests are run via the `pintos` Python script (added to PATH by `activate`), which launches QEMU:

```bash
cd pintos/threads
make check                        # run all tests for this module
pintos -- run <test-name>         # run a single test manually
```

Test source files live under `pintos/tests/<module>/`. Each test has a `.c` source, `.ck` expected-output checker, and a `Make.tests` file that registers it with the build system.

## Creating a Submission Archive

```bash
cd pintos
make TEAM=<team-number> archive   # produces a .tar.gz for submission
```

## Git Commit Convention

When creating commit messages in this repository, use the Angular Commit Convention format and write the message in Korean.

Recommended format:

```text
type(scope): 변경 내용
```

Examples:

```text
feat(threads): alarm clock 구현
fix(synch): priority donation 복원 로직 수정
chore(git): .gitattributes 추가 및 LF 줄바꿈 규칙 설정
```

## Architecture Overview

Pintos is an x86-64 educational kernel. Code is C with x86-64 assembly stubs. The kernel boots into 64-bit long mode and runs on QEMU.

### Module layout

| Directory | Purpose | Lab week |
|-----------|---------|----------|
| `threads/` | Thread struct, scheduler, sync primitives, interrupt handling | 1 |
| `userprog/` | User process load/exec/fork/wait, exception handling, syscalls | 2–3 |
| `vm/` | Supplemental page table, frame allocator, page eviction, swap | 4–5 |
| `filesys/` | Inode FS + FAT-based FS, directory, buffer cache | 6 |
| `devices/` | Disk, timer, keyboard, serial, VGA drivers | — |
| `lib/` | Shared data structures (list, hash, bitmap) used by kernel and user programs |
| `include/` | All header files mirroring the module structure |
| `tests/` | Per-module test suites |
| `utils/` | `pintos` launcher script, `backtrace` helper |

### Key interfaces

- **`threads/thread.h`** — `struct thread`, scheduler API (`thread_create`, `thread_block`, `thread_unblock`), priority range 0–63 (default 31), MLFQS support.
- **`threads/interrupt.h`** — ISR registration (`intr_register_int`, `intr_register_ext`), interrupt enable/disable.
- **`threads/synch.h`** — `struct lock`, `struct semaphore`, `struct condition`.
- **`userprog/process.h`** — `process_create_initd`, `process_fork`, `process_exec`, `process_wait`, `process_exit`.
- **`userprog/syscall.c`** — syscall dispatcher; system calls enter via the x86-64 `SYSCALL` instruction (MSR_LSTAR/MSR_STAR configured at boot).
- **`vm/vm.h`** — `struct supplemental_page_table`, page types (`VM_UNINIT`, `VM_ANON`, `VM_FILE`), frame management API.
- **`filesys/filesys.h`** — `filesys_init`, `filesys_create`, `filesys_open`. Compile with `-DEFILESYS` to enable the FAT-based FS (project 4).

### Memory model

- Thread pages are 4 KB: `struct thread` at the bottom, kernel stack growing down from the top.
- User virtual memory is separate; the supplemental page table (SPT) tracks mappings that aren't yet in physical frames.
- Kernel uses the large code model (`-mcmodel=large`) with no PIC and no SSE/AVX.

### Stub / TODO state

`vm/` and parts of `userprog/syscall.c` are intentional student stubs. Functions return placeholder values or `PANIC("TODO")`. Do not treat missing implementations as bugs.

## Online Manual

https://casys-kaist.github.io/pintos-kaist/
