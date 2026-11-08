// A `macro_rules!` definition exported to importers. The expander collects
// definitions from the files a source imports (before expanding it), so a `.hx`
// can carry a macro. In the generated (declaration-only) header the definition
// itself produces nothing.
macro_rules! define_pair {
    ($name:ident, $t:ty) => {
        struct $name {
            $t first;
            $t second;
        };

        $name $name##_make($t first, $t second) {
            $name p;
            p.first = first;
            p.second = second;
            return p;
        }
    };
}
