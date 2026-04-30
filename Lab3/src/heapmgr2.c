/*--------------------------------------------------------------------*/
/* heapmgr2.c                                                      */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "chunk.h"

#define FALSE 0
#define TRUE  1

#define NUM_BINS 10

enum { SYS_MIN_ALLOC_UNITS = 1024 };

/* Unused here, but declared extern in chunk.h. */
Chunk_T s_free_head = NULL;

static Chunk_T s_bins[NUM_BINS] = { NULL };

void *s_heap_lo = NULL, *s_heap_hi = NULL;
int s_heap_booted = FALSE;

static size_t
bytes_to_payload_units(size_t bytes)
{
    return (bytes + (CHUNK_UNIT - 1)) / CHUNK_UNIT;
}

static Chunk_T
header_from_payload(void *p)
{
    return (Chunk_T)((char *)p - CHUNK_UNIT);
}

static void
heap_bootstrap(void)
{
    if (s_heap_booted) return;
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) {
        fprintf(stderr, "sbrk(0) failed\n");
        exit(-1);
    }
    s_heap_booted = TRUE;
}

static int
get_bin_index(int span_units)
{
    if (span_units < 8) return 0;
    if (span_units < 16) return 1;
    if (span_units < 32) return 2;
    if (span_units < 64) return 3;
    if (span_units < 128) return 4;
    if (span_units < 256) return 5;
    if (span_units < 512) return 6;
    if (span_units < 1024) return 7;
    if (span_units < 2048) return 8;
    return 9;
}

#ifndef NDEBUG
static int
check_heap_validity(void)
{
    int i;
    Chunk_T w;
    char *expected_end;

    if (s_heap_lo == NULL) {
        fprintf(stderr, "Uninitialized heap start\n");
        return FALSE;
    }
    if (s_heap_hi == NULL) {
        fprintf(stderr, "Uninitialized heap end\n");
        return FALSE;
    }

    if (s_heap_lo == s_heap_hi) {
        for (i = 0; i < NUM_BINS; i++) {
            if (s_bins[i] != NULL) {
                fprintf(stderr, "Inconsistent empty heap: bin %d not empty\n", i);
                return FALSE;
            }
        }
        return TRUE;
    }

    expected_end = (char *)s_heap_lo;
    for (w = (Chunk_T)s_heap_lo;
         w && w < (Chunk_T)s_heap_hi;
         w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi))
            return FALSE;
        expected_end = (char *)w + (size_t)chunk_get_span_units(w) * (size_t)CHUNK_UNIT;
    }
    if (expected_end != (char *)s_heap_hi) {
        fprintf(stderr, "Physical block walk did not end at s_heap_hi\n");
        return FALSE;
    }

    for (i = 0; i < NUM_BINS; i++) {
        Chunk_T head = s_bins[i];

        if (head && chunk_get_prev_free(head) != NULL) {
            fprintf(stderr, "Bin %d head has non-NULL prev\n", i);
            return FALSE;
        }

        for (w = head; w; w = chunk_get_next_free(w)) {
            Chunk_T n;

            if (chunk_get_status(w) != CHUNK_FREE) {
                fprintf(stderr, "Non-free chunk in bin %d\n", i);
                return FALSE;
            }
            if (!chunk_is_valid(w, s_heap_lo, s_heap_hi))
                return FALSE;
            if (get_bin_index(chunk_get_span_units(w)) != i) {
                fprintf(stderr, "Wrong bin for span in bin %d\n", i);
                return FALSE;
            }

            n = chunk_get_adjacent(w, s_heap_lo, s_heap_hi);
            if (n != NULL && chunk_get_status(n) == CHUNK_FREE) {
                fprintf(stderr, "Uncoalesced adjacent free chunks\n");
                return FALSE;
            }

            n = chunk_get_next_free(w);
            if (n != NULL && chunk_get_prev_free(n) != w) {
                fprintf(stderr, "Next->prev symmetry broken in bin %d\n", i);
                return FALSE;
            }

            n = chunk_get_prev_free(w);
            if (n == NULL) {
                if (w != head) {
                    fprintf(stderr, "Non-head node with NULL prev in bin %d\n", i);
                    return FALSE;
                }
            }
            else if (chunk_get_next_free(n) != w) {
                fprintf(stderr, "Prev->next symmetry broken in bin %d\n", i);
                return FALSE;
            }
        }
    }

    return TRUE;
}
#endif

static Chunk_T
split_for_alloc(Chunk_T c, size_t need_units)
{
    Chunk_T alloc;
    Chunk_T c_prev;
    int old_span;
    int alloc_span;
    int remain_span;

    assert(c != NULL);

    c_prev = chunk_get_prev_free(c);
    old_span = chunk_get_span_units(c);
    alloc_span = (int)(2 + need_units);
    remain_span = old_span - alloc_span;

    assert(old_span >= 2);
    assert(alloc_span >= 2);
    assert(remain_span >= 2);
    assert(c >= (Chunk_T)s_heap_lo && c < (Chunk_T)s_heap_hi);
    assert(chunk_get_status(c) == CHUNK_FREE);

    if (c_prev != NULL) {
        assert(chunk_get_status(c_prev) == CHUNK_FREE);
        assert(chunk_get_next_free(c_prev) == c);
    }

    chunk_set_span_units(c, remain_span);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));

    alloc = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
    assert(alloc != NULL);
    assert((void *)alloc >= s_heap_lo && (void *)alloc < s_heap_hi);

    chunk_set_span_units(alloc, alloc_span);
    chunk_set_status(alloc, CHUNK_USED);
    chunk_set_next_free(alloc, NULL);
    chunk_set_prev_free(alloc, NULL);

    chunk_set_prev_free(c, c_prev);

    assert(chunk_get_adjacent(c, s_heap_lo, s_heap_hi) == alloc);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_is_valid(alloc, s_heap_lo, s_heap_hi));

    return alloc;
}

static void
bin_detach(Chunk_T c)
{
    Chunk_T prev;
    Chunk_T next;
    int bin_index;

    assert(c != NULL);
    assert(chunk_get_status(c) == CHUNK_FREE);

    prev = chunk_get_prev_free(c);
    next = chunk_get_next_free(c);
    bin_index = get_bin_index(chunk_get_span_units(c));

    if (prev == NULL) {
        assert(s_bins[bin_index] == c);
        s_bins[bin_index] = next;
        if (next != NULL)
            chunk_set_prev_free(next, NULL);
    }
    else {
        chunk_set_next_free(prev, next);
        if (next != NULL)
            chunk_set_prev_free(next, prev);
    }

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
}

static Chunk_T
coalesce_two(Chunk_T a, Chunk_T b)
{
    if (b < a) {
        Chunk_T t = a;
        a = b;
        b = t;
    }

    assert(a != NULL && b != NULL && a != b);
    assert(chunk_get_status(a) == CHUNK_FREE);
    assert(chunk_get_status(b) == CHUNK_FREE);
    assert(chunk_get_adjacent(a, s_heap_lo, s_heap_hi) == b);
    assert(chunk_get_prev_adjacent(b, s_heap_lo, s_heap_hi) == a);

    chunk_set_span_units(a, chunk_get_span_units(a) + chunk_get_span_units(b));
    chunk_set_prev_free(a, NULL);
    chunk_set_next_free(a, NULL);

    return a;
}

static void
bin_push_front(Chunk_T c)
{
    Chunk_T next;
    Chunk_T prev;
    int bin_index;

    assert(c != NULL);
    assert((void *)c >= s_heap_lo && (void *)c < s_heap_hi);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));

    chunk_set_status(c, CHUNK_FREE);
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, NULL);

    next = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
    if (next != NULL && chunk_get_status(next) == CHUNK_FREE) {
        bin_detach(next);
        c = coalesce_two(c, next);
    }

    prev = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi);
    if (prev != NULL &&
        chunk_is_valid(prev, s_heap_lo, s_heap_hi) &&
        chunk_get_adjacent(prev, s_heap_lo, s_heap_hi) == c &&
        chunk_get_status(prev) == CHUNK_FREE) {
        bin_detach(prev);
        c = coalesce_two(prev, c);
    }

    bin_index = get_bin_index(chunk_get_span_units(c));
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, s_bins[bin_index]);
    if (s_bins[bin_index] != NULL)
        chunk_set_prev_free(s_bins[bin_index], c);
    s_bins[bin_index] = c;

    assert(chunk_get_prev_free(s_bins[bin_index]) == NULL);
}

static Chunk_T
sys_grow_and_link(size_t need_units)
{
    Chunk_T c;
    size_t grow_data;
    size_t grow_span;

    grow_data = (need_units < SYS_MIN_ALLOC_UNITS)
        ? SYS_MIN_ALLOC_UNITS
        : need_units;
    grow_span = 2 + grow_data;

    c = (Chunk_T)sbrk(grow_span * CHUNK_UNIT);
    if ((void *)c == (void *)-1)
        return NULL;

    s_heap_hi = sbrk(0);

    chunk_set_span_units(c, (int)grow_span);
    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
    chunk_set_status(c, CHUNK_FREE);

    /* Let the normal insertion path handle any coalescing safely. */
    bin_push_front(c);

    return c;
}

void *
heapmgr_malloc(size_t size)
{
    Chunk_T cur;
    size_t need_units;
    int i;
    int bin_index;

    if (size == 0)
        return NULL;

    heap_bootstrap();
    assert(check_heap_validity());

    need_units = bytes_to_payload_units(size);
    bin_index = get_bin_index(2 + (int)need_units);

    for (i = bin_index; i < NUM_BINS; i++) {
        for (cur = s_bins[i]; cur != NULL; cur = chunk_get_next_free(cur)) {
            size_t cur_payload = (size_t)chunk_get_span_units(cur) - 2;

            if (cur_payload >= need_units) {
                bin_detach(cur);
                if ((size_t)chunk_get_span_units(cur) >= (size_t)(2 + need_units + 2)) {
                    Chunk_T alloc = split_for_alloc(cur, need_units);
                    bin_push_front(cur);
                    assert(check_heap_validity());
                    return (char *)alloc + CHUNK_UNIT;
                }

                chunk_set_status(cur, CHUNK_USED);
                assert(check_heap_validity());
                return (char *)cur + CHUNK_UNIT;
            }
        }
    }

    cur = sys_grow_and_link(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    /* Find the grown/coalesced block in its bin, then allocate from it. */
    bin_index = get_bin_index((int)(2 + ((need_units < SYS_MIN_ALLOC_UNITS)
        ? SYS_MIN_ALLOC_UNITS
        : need_units)));
    for (i = bin_index; i < NUM_BINS; i++) {
        for (cur = s_bins[i]; cur != NULL; cur = chunk_get_next_free(cur)) {
            size_t cur_payload = (size_t)chunk_get_span_units(cur) - 2;

            if (cur_payload >= need_units) {
                bin_detach(cur);
                if ((size_t)chunk_get_span_units(cur) >= (size_t)(2 + need_units + 2)) {
                    Chunk_T alloc = split_for_alloc(cur, need_units);
                    bin_push_front(cur);
                    assert(check_heap_validity());
                    return (char *)alloc + CHUNK_UNIT;
                }

                chunk_set_status(cur, CHUNK_USED);
                assert(check_heap_validity());
                return (char *)cur + CHUNK_UNIT;
            }
        }
    }

    assert(FALSE);
    return NULL;
}

void
heapmgr_free(void *p)
{
    Chunk_T c;

    assert(s_heap_lo != NULL && s_heap_hi != NULL);
    assert((p == NULL) || ((void *)p >= s_heap_lo && (void *)p < s_heap_hi));

    if (p == NULL)
        return;

    assert(check_heap_validity());

    c = header_from_payload(p);
    assert(c != NULL);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(c) == CHUNK_USED);
    assert(chunk_get_next_free(c) == NULL && chunk_get_prev_free(c) == NULL);

    bin_push_front(c);

    assert(chunk_get_status(c) == CHUNK_FREE);
    assert(check_heap_validity());
}
