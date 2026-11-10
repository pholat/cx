sm Machine {
    [*] -> playing : start / log_play
    playing -> done : finish [can_stop] / log_stopped
    // Runs when the guarded transition above does not.
    playing -> done : finish / log_wont_stop
    done -> [*] / log_done
    [any] -> [current] / log_unexpected
}

#[str] enum Machine::State;
#[str] enum Machine::Event;
