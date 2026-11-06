namespace demo {

    struct Counter {
        int value;

        Counter(int v) {
            value = v;
        }

        ~Counter() {
            reset(value);
        }
    };

    int reset(int v);
    void run();
}
