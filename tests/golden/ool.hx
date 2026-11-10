namespace ool {

    /* Declaration-only header: the API lives here, the constructor/destructor
     * bodies live in `ool.cx` (a header never emits a definition for them).
     *
     * A constructor needs a body placeholder (`{ }`); a destructor may be
     * written `~Counter();` (see `technical.md` §7). */
    struct Counter {
        int value;

        Counter(int v) { }
        ~Counter();
    };

    int run();
}
