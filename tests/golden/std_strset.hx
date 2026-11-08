#include "khash.h"

// A klib-backed set of `const char *` values (`KHASH_SET_INIT_STR`):
//
//     import "std_strset.hx";
//     define_str_set!(NameSet);
//
// Keys are not copied by klib, so the caller keeps them alive (string literals
// are fine). Methods use the `.`-method spelling — `s.add("k")` (returns whether
// the element was new), `s.has("k")`, `s.del("k")`, `s.size()`, `s.free()`.
raw {
    #define CX_KHASH_STRSET_HELPERS(name) \
        static int cx_##name##_add(void *h, const char *key) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            int absent = 0; \
            kh_put_##name(t, key, &absent); \
            return absent; \
        } \
        static int cx_##name##_has(void *h, const char *key) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            return kh_get_##name(t, key) != kh_end(t); \
        } \
        static void cx_##name##_del(void *h, const char *key) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            khint_t it = kh_get_##name(t, key); \
            if (it != kh_end(t)) kh_del_##name(t, it); \
        }
}

macro_rules! define_str_set {
    ($name:ident) => {
        KHASH_SET_INIT_STR!($name);
        CX_KHASH_STRSET_HELPERS!($name);

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
        int $name::add($name *s, const char *value) {
            int added = cx_##$name##_add(s->h, value);
            if (added != 0) {
                s->n += 1;
            }
            return added;
        }

        int $name::has($name *s, const char *value) {
            return cx_##$name##_has(s->h, value);
        }

        void $name::del($name *s, const char *value) {
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
