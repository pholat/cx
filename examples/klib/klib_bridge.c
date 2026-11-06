/* Hand-written glue between klib's macro/pointer-based containers and the
 * pointer-free Cx wrappers in list.cx / map.cx.
 *
 * Build with the klib headers on the include path, e.g.:
 *     cc -std=gnu11 -Isubmodules/klib -c klib_bridge.c
 */
#include "klib_bridge.h"

#include "klist.h"
#include "khash.h"

#include <stdlib.h>

/* ------------------------------------------------------------ handle table */
typedef struct {
    void *ptr;
    int   next_free;
} kl_handle_slot;

static kl_handle_slot *g_slots = NULL;
static int g_slots_len = 0;
static int g_slots_cap = 0;
static int g_free_head = -1;

static int handle_alloc(void *ptr) {
    int idx;
    if (g_free_head >= 0) {
        idx = g_free_head;
        g_free_head = g_slots[idx].next_free;
    } else {
        if (g_slots_len == g_slots_cap) {
            int cap = g_slots_cap ? g_slots_cap * 2 : 16;
            kl_handle_slot *s =
                (kl_handle_slot *)realloc(g_slots, sizeof(kl_handle_slot) * (size_t)cap);
            if (!s) return 0;
            g_slots = s;
            g_slots_cap = cap;
        }
        idx = g_slots_len++;
    }
    g_slots[idx].ptr = ptr;
    return idx + 1;
}

static void *handle_get(int handle) {
    if (handle <= 0 || handle > g_slots_len) return NULL;
    return g_slots[handle - 1].ptr;
}

static void handle_release(int handle) {
    if (handle <= 0 || handle > g_slots_len) return;
    g_slots[handle - 1].ptr = NULL;
    g_slots[handle - 1].next_free = g_free_head;
    g_free_head = handle - 1;
}

/* ---------------------------------------------------------- list of ints */
static void klint_nofree(void *p) { (void)p; } /* int payload: nothing to free */
KLIST_INIT(klint, int, klint_nofree)

int kl_list_new(void) {
    return handle_alloc(kl_init(klint));
}

void kl_list_free(int handle) {
    klist_t(klint) *l = (klist_t(klint) *)handle_get(handle);
    if (!l) return;
    kl_destroy(klint, l);
    handle_release(handle);
}

int kl_list_size(int handle) {
    klist_t(klint) *l = (klist_t(klint) *)handle_get(handle);
    return l ? (int)l->size : 0;
}

int kl_list_empty(int handle) {
    return kl_list_size(handle) == 0;
}

void kl_list_push(int handle, int value) {
    klist_t(klint) *l = (klist_t(klint) *)handle_get(handle);
    if (l) *kl_pushp(klint, l) = value;
}

int kl_list_get(int handle, int index) {
    klist_t(klint) *l = (klist_t(klint) *)handle_get(handle);
    if (!l || index < 0) return 0;
    kliter_t(klint) *it = kl_begin(l);
    for (; it != kl_end(l) && index > 0; it = kl_next(it)) index--;
    return it != kl_end(l) ? kl_val(it) : 0;
}

int kl_list_shift(int handle, int fallback) {
    klist_t(klint) *l = (klist_t(klint) *)handle_get(handle);
    int v = fallback;
    if (!l) return fallback;
    return kl_shift(klint, l, &v) == 0 ? v : fallback;
}

/* ------------------------------------------------------------- int->int map */
KHASH_MAP_INIT_INT(klmap, int)

int kl_map_new(void) {
    return handle_alloc(kh_init(klmap));
}

void kl_map_free(int handle) {
    khash_t(klmap) *m = (khash_t(klmap) *)handle_get(handle);
    if (!m) return;
    kh_destroy(klmap, m);
    handle_release(handle);
}

int kl_map_size(int handle) {
    khash_t(klmap) *m = (khash_t(klmap) *)handle_get(handle);
    return m ? (int)kh_size(m) : 0;
}

int kl_map_empty(int handle) {
    return kl_map_size(handle) == 0;
}

void kl_map_put(int handle, int key, int value) {
    khash_t(klmap) *m = (khash_t(klmap) *)handle_get(handle);
    if (!m) return;
    int ret = 0;
    khint_t k = kh_put(klmap, m, key, &ret);
    if (ret >= 0) kh_val(m, k) = value;
}

int kl_map_get(int handle, int key, int fallback) {
    khash_t(klmap) *m = (khash_t(klmap) *)handle_get(handle);
    if (!m) return fallback;
    khint_t k = kh_get(klmap, m, key);
    return k == kh_end(m) ? fallback : kh_val(m, k);
}

int kl_map_has(int handle, int key) {
    khash_t(klmap) *m = (khash_t(klmap) *)handle_get(handle);
    if (!m) return 0;
    return kh_get(klmap, m, key) != kh_end(m);
}

void kl_map_del(int handle, int key) {
    khash_t(klmap) *m = (khash_t(klmap) *)handle_get(handle);
    if (!m) return;
    khint_t k = kh_get(klmap, m, key);
    if (k != kh_end(m)) kh_del(klmap, m, k);
}
