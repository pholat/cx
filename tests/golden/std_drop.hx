// The `Drop` interface: an element type that may be stored in an owning
// container (`std_vec.hx`) implements it. Import this module, declare
// `implements Drop` on the element type, and an owning container's `free` will
// invoke `drop` on every element through the installed vtable.
//
//     import "std_drop.hx";
//
//     struct Box {
//         int v;
//         implements Drop {
//             void drop(self) { self->v = 0; }
//         }
//     };
trait Drop {
    void drop(self);
}
