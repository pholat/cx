#include "klist.h"

// A klib-backed list container, exported as a macro *module* (macros now cross
// `import` boundaries). Import it and instantiate per element type:
//
//     import "std_list.hx";
//     define_list!(IntList, int);
//
// The generated methods use the `.`-method spelling — `l.push(1)`, `l.size()`,
// `l.pop(0)`, `l.free()`. `KLIST_INIT!` is the C-macro passthrough; the wrapper
// stores the klist as `void *` because Cx types cannot express `klist_t(<Name>)`.
// The C context this module needs (`#include "klist.h"` above and the pool free
// callback below) rides along in the generated header, so the importer needs
// nothing extra.
raw {
    // A no-op klist pool free callback (it applies to internal nodes, not element
    // data). A macro, so importing several `std_*.hx` modules that share the
    // name is a benign identical redefinition.
    #define cx_nofree(p) ((void)(p))
}

macro_rules! define_list {
    ($name:ident, $t:ty) => {
        KLIST_INIT!($name, $t, cx_nofree);

        struct $name {
            void *v;
            int n;
        };

        $name $name::new() {
            $name l;
            l.v = kl_init_##$name();
            l.n = 0;
            return l;
        }

        void $name::push($name *l, $t value) {
            $t *p = kl_pushp_##$name(l->v);
            *p = value;
            l->n += 1;
        }

        $t $name::pop($name *l, $t fallback) {
            $t out;
            if (kl_shift_##$name(l->v, &out) == 0) {
                l->n -= 1;
                return out;
            }
            return fallback;
        }

        int $name::size($name *l) {
            return l->n;
        }

        void $name::free($name *l) {
            kl_destroy_##$name(l->v);
            l->v = 0;
            l->n = 0;
        }
    };
}
