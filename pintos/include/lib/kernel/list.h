#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 이중 연결 리스트.
 *
 * 이 리스트 구현은 리스트 노드마다 별도의 동적 메모리를 할당하지 않는다.
 * 대신 리스트에 들어갈 수 있는 각 구조체가 struct list_elem 멤버를 직접
 * 포함해야 한다. 모든 리스트 함수는 이 struct list_elem을 기준으로 동작한다.
 * list_entry 매크로는 struct list_elem 포인터를 그것을 포함하는 바깥 구조체
 * 포인터로 되돌릴 때 사용한다.

 * 예를 들어 struct foo 목록이 필요하다면 struct foo 안에 struct list_elem
 * 멤버를 다음처럼 포함시킨다.

 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...다른 멤버들...
 * };

 * 그런 다음 struct foo를 담을 리스트를 다음처럼 선언하고 초기화할 수 있다.

 * struct list foo_list;

 * list_init (&foo_list);

 * 순회할 때는 struct list_elem에서 그것을 포함하는 구조체로 되돌리는 작업이
 * 자주 필요하다. foo_list를 순회하는 예시는 다음과 같다.

 * struct list_elem *e;

 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...f를 사용한다...
 * }

 * 실제 사용 예시는 소스 곳곳에서 볼 수 있다. 예를 들어 threads 디렉터리의
 * malloc.c, palloc.c, thread.c가 모두 리스트를 사용한다.

 * 이 리스트 인터페이스는 C++ STL의 list<> 템플릿에서 영향을 받았다.
 * 다만 C 구현이므로 타입 검사를 하지 않으며, 잘못된 elem을 넣는 실수도
 * 대부분 런타임 전까지 잡아주지 못한다.

 * 리스트 용어:

 * - "front": 리스트의 첫 번째 실제 원소. 빈 리스트에서는 정의되지 않는다.
 * list_front()가 반환한다.

 * - "back": 리스트의 마지막 실제 원소. 빈 리스트에서는 정의되지 않는다.
 * list_back()이 반환한다.

 * - "tail": 리스트의 마지막 원소 바로 뒤에 있다고 보는 센티널 원소.
 * 빈 리스트에서도 항상 존재한다. list_end()가 반환하며, 앞에서 뒤로
 * 순회할 때 종료 지점으로 사용한다.

 * - "beginning": 빈 리스트가 아니면 front, 빈 리스트이면 tail이다.
 * list_begin()이 반환하며, 앞에서 뒤로 순회할 때 시작 지점으로 사용한다.

 * - "head": 리스트의 첫 번째 원소 바로 앞에 있다고 보는 센티널 원소.
 * 빈 리스트에서도 항상 존재한다. list_rend()가 반환하며, 뒤에서 앞으로
 * 순회할 때 종료 지점으로 사용한다.

 * - "reverse beginning": 빈 리스트가 아니면 back, 빈 리스트이면 head이다.
 * list_rbegin()이 반환하며, 뒤에서 앞으로 순회할 때 시작 지점으로 사용한다.
 *
 * - "interior element": head나 tail 센티널이 아닌 실제 리스트 원소.
 * 빈 리스트에는 interior element가 없다. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트에 연결되는 개별 원소. */
struct list_elem {
	struct list_elem *prev;     /* 이전 리스트 원소. */
	struct list_elem *next;     /* 다음 리스트 원소. */
};

/* 리스트 전체를 나타내는 구조체. */
struct list {
	struct list_elem head;      /* 리스트 앞쪽 센티널. */
	struct list_elem tail;      /* 리스트 뒤쪽 센티널. */
};

/* LIST_ELEM 포인터를 그것이 포함된 바깥 구조체 포인터로 변환한다.
   STRUCT에는 바깥 구조체 이름을, MEMBER에는 그 안의 list_elem 멤버 이름을
   넘긴다. 사용 예시는 파일 상단 설명을 참고한다. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* 리스트 순회. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* 리스트 삽입. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* 리스트 제거. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* 리스트 원소 접근. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* 리스트 상태 조회. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* 기타 연산. */
void list_reverse (struct list *);

/* 보조 데이터 AUX를 기준으로 리스트 원소 A와 B를 비교한다.
   A가 B보다 작으면 true를, A가 B보다 크거나 같으면 false를 반환한다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* 정렬된 원소를 다루는 리스트 연산. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* 최댓값과 최솟값 조회. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
