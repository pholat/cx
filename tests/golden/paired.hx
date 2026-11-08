namespace demo {

    trait Printable {
        void print(self);
    }

    struct Counter {
        int value;

        Counter(int v) {
            value = v;
        }

        ~Counter() {
            value = 0;
        }

        implements Printable {
            void print(self) {
                value += 1;
            }
        }
    };

    int reset(int v);
    int run();
}
