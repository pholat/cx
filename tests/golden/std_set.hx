#include "khash.h"

// A klib-backed set of `int` values, exported as a macro module:
//
//     import "std_set.hx";
//     define_int_set!(IntSet);
//
// `KHASH_SET_INIT_INT!` is a C-macro passthrough; a per-instantiation C helper
// wraps the `void *` handle. Methods use the `.`-method spelling — `s.add(1)`
// (returns whether the element was new), `s.has(1)`, `s.del(1)`, `s.size()`,
// `s.free()`.
raw {
    #define CX_KHASH_SET_HELPERS(name) \
        static int cx_##name##_add(void *h, int key) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            int absent = 0; \
            kh_put_##name(t, key, &absent); \
            return absent; \
        } \
        static int cx_##name##_has(void *h, int key) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            return kh_get_##name(t, key) != kh_end(t); \
        } \
        static void cx_##name##_del(void *h, int key) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            khint_t it = kh_get_##name(t, key); \
            if (it != kh_end(t)) kh_del_##name(t, it); \
        }
}

macro_rules! define_int_set {
    ($name:ident) => {
        KHASH_SET_INIT_INT!($name);
        CX_KHASH_SET_HELPERS!($name);

        struct $name {
            void *h;
            int n;
        };

        $name $name::new() {
            $name s;
            s.h = kh_init_##$name();
            s.n = 0;
            return s;
        }

        // Insert `value`; returns 1 if it was not already present.
        int $name::add($name *s, int value) {
            int added = cx_##$name##_add(s->h, value);
            if (added != 0) {
                s->n += 1;
            }
            return added;
        }

        int $name::has($name *s, int value) {
            return cx_##$name##_has(s->h, value);
        }

        void $name::del($name *s, int value) {
            if (cx_##$name##_has(s->h, value) != 0) {
                cx_##$name##_del(s->h, value);
                s->n -= 1;
            }
        }

        int $name::size($name *s) {
            return s->n;
        }

        void $name::free($name *s) {
            kh_destroy_##$name(s->h);
            s->h = 0;
            s->n = 0;
        }
    };
}
