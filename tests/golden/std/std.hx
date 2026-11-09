// The std catalog. Import this to get the `for!` lowering over the `len`/`at`
// protocol.
//
//     import "std/std.hx";
//
//     for!(x, list, {
//         total += x;
//     });
//
// `for!` expands to an indexed loop over `.len()` / `.at(i)`, binding each
// element to a user-named local with `auto` (inferred from `.at`).
macro_rules! for {
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
