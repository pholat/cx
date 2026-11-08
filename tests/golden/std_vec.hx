#include "klist.h"

// An element-typed, klib-backed vector of `$t *` elements. The element type must
// implement the `Drop` interface (checked after expansion/sema), and `free`
// invokes `drop` on every stored element through the installed vtable. Import
// `std_drop.hx` for the interface, then instantiate:
//
//     import "std_drop.hx";
//     import "std_vec.hx";
//     define_vec!(BoxVec, Box);
//
// The generated methods use the `.`-method spelling — `v.push(&item)`,
// `v.size()`, `v.free()`. klist stores `void *` slots, so the concrete element
// type appears only in Cx-generated code and never inside the klib C macro.
raw {
    // A no-op klist pool free callback (it applies to internal nodes, not element
    // data). A macro, so importing several `std_*.hx` modules that share the
    // name is a benign identical redefinition.
    #define cx_nofree(p) ((void)(p))
}

macro_rules! define_vec {
    ($name:ident, $t:ty) requires $t: Drop => {
        KLIST_INIT!($name, void *, cx_nofree);

        struct $name {
            void *v;
            int n;
        };

        $name $name::new() {
            $name b;
            b.v = kl_init_##$name();
            b.n = 0;
            return b;
        }

        void $name::push($name *b, $t *item) {
            $t **slot = ($t **) kl_pushp_##$name(b->v);
            *slot = item;
            b->n += 1;
        }

        int $name::size($name *b) {
            return b->n;
        }

        void $name::free($name *b) {
            // Element teardown: pop each stored `$t *` and invoke its `Drop`
            // method through the installed vtable.
            void *item;
            while (kl_shift_##$name(b->v, &item) == 0) {
                $t *p = ($t *) item;
                void (*dropfn)(void *) = p->Drop->drop;
                dropfn(p);
            }
            kl_destroy_##$name(b->v);
            b->v = 0;
            b->n = 0;
        }
    };
}
