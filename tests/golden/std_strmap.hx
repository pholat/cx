#include "khash.h"

// A klib-backed, string-keyed map, exported as a macro module:
//
//     import "std_strmap.hx";
//     define_str_map!(NameMap, int);
//
// `KHASH_MAP_INIT_STR` keys are `const char *` and are *not* copied by klib, so
// the caller keeps them alive (string literals are fine). A per-instantiation C
// helper wraps the `void *` handle. Methods use the `.`-method spelling —
// `m.put("k", 1)`, `m.get("k", 0)`, `m.has("k")`, `m.del("k")`, `m.size()`,
// `m.free()`.
raw {
    #define CX_KHASH_STRMAP_HELPERS(name, val_t) \
        static int cx_##name##_put(void *h, const char *key, val_t value) { \
            int ok = 0; \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            khint_t it = kh_put_##name(t, key, &ok); \
            t->vals[it] = value; \
            return ok; \
        } \
        static val_t cx_##name##_get(void *h, const char *key, val_t fallback) { \
            kh_##name##_t *t = (kh_##name##_t *)h; \
            khint_t it = kh_get_##name(t, key); \
            if (it == kh_end(t)) return fallback; \
            return t->vals[it]; \
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

macro_rules! define_str_map {
    ($name:ident, $v:ty) => {
        KHASH_MAP_INIT_STR!($name, $v);
        CX_KHASH_STRMAP_HELPERS!($name, $v);

        struct $name {
            void *h;
            int n;
        };

        $name $name::new() {
            $name m;
            m.h = kh_init_##$name();
            m.n = 0;
            return m;
        }

        void $name::put($name *m, const char *key, $v value) {
            int added = cx_##$name##_put(m->h, key, value);
            if (added != 0) {
                m->n += 1;
            }
        }

        $v $name::get($name *m, const char *key, $v fallback) {
            return cx_##$name##_get(m->h, key, fallback);
        }

        int $name::has($name *m, const char *key) {
            return cx_##$name##_has(m->h, key);
        }

        void $name::del($name *m, const char *key) {
            if (cx_##$name##_has(m->h, key) != 0) {
                cx_##$name##_del(m->h, key);
                m->n -= 1;
            }
        }

        int $name::size($name *m) {
            return m->n;
        }

        void $name::free($name *m) {
            kh_destroy_##$name(m->h);
            m->h = 0;
            m->n = 0;
        }
    };
}
