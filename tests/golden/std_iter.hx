// The iterator protocol: a container that can be walked exposes `len` and
// `at` (both plain instance methods, so `.`-method calls resolve to
// `<Type>__len` / `<Type>__at`).
//
// `for_each!` is the `for`-over-containers lowering: it expands to an indexed
// loop over `.len()` calling `.at(i)`, binding each element to a user-named
// local with `auto` (the element type is inferred from `.at`). This works with
// any type that provides the two methods.
//
//     import "std_iter.hx";
//
//     for_each!(x, list, {
//         total += x;
//     });
macro_rules! for_each {
    ($elem:ident, $seq:expr, $body:block) => {
        {
            int __cx_n = ($seq).len();
            int __cx_i = 0;
            for (; __cx_i < __cx_n; __cx_i += 1) {
                auto $elem = ($seq).at(__cx_i);
                $body
            }
        }
    };
}

