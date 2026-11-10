import "sm.hx";

int can_stop(Machine *m, Machine::State from, Machine::State to, Machine::Event event);
void log_play(Machine *m, Machine::State from, Machine::State to, Machine::Event event);
void log_stopped(Machine *m, Machine::State from, Machine::State to, Machine::Event event);
void log_wont_stop(Machine *m, Machine::State from, Machine::State to, Machine::Event event);
void log_done(Machine *m, Machine::State from, Machine::State to, Machine::Event event);
void log_unexpected(Machine *m, Machine::State from, Machine::State to, Machine::Event event);
