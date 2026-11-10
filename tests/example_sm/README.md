# Runnable state-machine example

A split, runnable example of the Cx **`sm`** (state machine) syntax: `example_sm.cx` wires the app, `calls.cx` holds the callbacks, `sm.cx` holds the machine, and the matching `*.hx` files expose the shared declarations. The build writes all generated files into `build/`.

```text
example_sm.cx + calls.cx + sm.cx + calls.hx + sm.hx  --cx build-->  build/example_sm.c + build/calls.c + build/sm.c  --cc-->  build/example_sm.out
```

## Build & run

```sh
make            # build the generated headers, C files and build/example_sm.out
make run        # build then execute
make c          # build the generated C files only
```

`cx` must be built first (`make` in the repository root).

Expected output:

```text
machine with can_stop() = 1:
  entry - send finish (no [*] handler):
    action log_unexpected: entry -> entry (finish)
  send start:
    action log_play: entry -> playing (start)
  send finish (guard = 1):
    guard can_stop: playing -> done (finish) -> 1
    action log_stopped: playing -> done (finish)
  send start in done:
    action log_done: done -> entry (start)
  final state = entry

machine with can_stop() = 0 (fallback action runs):
  entry - send finish (no [*] handler):
    action log_unexpected: entry -> entry (finish)
  send start:
    action log_play: entry -> playing (start)
  send finish (guard = 0):
    guard can_stop: playing -> done (finish) -> 0
    action log_wont_stop: playing -> done (finish)
  send start in done:
    action log_done: done -> entry (start)
  final state = entry
```

## What it demonstrates

| Feature | Showed by |
| :-- | :-- |
| `sm` machine | `sm Machine { … }` with `[*]` entry, `[any]` fallback and `[current]` |
| Cx-qualified enums | `Machine::State` / `Machine::Event` in callback signatures; `Machine::event::start` for events |
| `#[str]` names | explicit `#[str] enum Machine::State;` / `#[str] enum Machine::Event;` markers opt in to `Machine::State::c_str(...)` / `Machine::Event::c_str(...)`, used in the `printf` lines (names, not numbers) |
| events are enums | `Machine::event::start` — a constant, not assignable |
| guards + actions | assigned on the struct (`m.guard_can_stop = can_stop;`) |
| full callback context | every guard/action gets the machine, the **`from`** state, the **`to`** state and the **event** (all typed enums); `m->ctx` is the `void *` user context |
| first-match-wins | `playing -> done : finish [can_stop]` then `playing -> done : finish` (the fallback) |
| C interop | `#include <stdio.h>` + `printf`, `#[export("main")]` for the C `main` |

## Notes

* `sm.cx` contains the machine implementation. `sm.hx` exposes the matching
  declarations, and `calls.cx` contains the callbacks. `make` writes the
  generated headers, C files and executable into `build/`.
* The `c_str` helpers are opt-in: the `#[str] enum Machine::State;` /
  `#[str] enum Machine::Event;` lines right after the `sm` block request them,
  and codegen emits them there (after the machine, before the functions).
* The machine's generated C names use cx-style `__` sub-namespacing, but the
  `.cx` file uses the Cx spellings: states `Machine::State`
  (`Machine__State`), events `Machine::Event` (`Machine__Event`), event
  constants `Machine::event::<e>` (`Machine__event__<e>`), states
  `Machine::state::<s>` (`Machine__state__<s>`), guard/action fields
  `guard_<g>` / `action_<a>`, and the API `Machine__init` / `__reset` /
  `__get_state` / `__process_event`.
* `m.init(&allowed)`, `m.process_event(…)` and `m.get_state()` are `.`-method
  calls; the transpiler lowers them to the associated `Machine__…` functions.
* Events are enum constants and cannot be assigned; only guards and actions are
  assignable callbacks.
