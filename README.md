# Cycle-Accurate Order Book Checker — A Golden Model With a Clock

Hardware teams verify a matching-engine design by running it against a
**golden reference model** one clock cycle at a time, with a **scoreboard**
that checks not just *what* the design outputs but *when* — a trade that
appears one cycle late is a bug, even if its price and quantity are right.

This project applies that methodology, in C++20, to a production-style
limit order book matching engine
([limit-order-book](https://github.com/abdulkhaliqfarah/limit-order-book)).
A naive golden book is stepped through a modeled 3-stage pipeline —
**fetch → decode → execute** — one feed message per cycle, and the
scoreboard demands cycle-accurate equivalence with the real engine:

- the trade sequences are identical, field by field;
- every trade is emitted at **exactly** the predicted cycle
  (injection cycle + 2) — none early, none late, none from the wrong
  message;
- the final books agree: live order count, best bid/ask, and resting
  quantity at every probed price.

> **Scope, stated plainly:** this is the golden-model/scoreboard
> *methodology* implemented in software — a cycle-stepped reference model,
> not an HDL design. There is no Verilog in this repo and it does not claim
> to be one. The pipeline is a timing model: it exists so "correct output
> at the wrong cycle" is a detectable failure, the property that matters
> when this style of engine is taken to hardware.

## Quick start

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                        # 81 scenario checks
./build/sweep 50 5000                         # randomized equivalence sweep
python3 scripts/model.py ./build/run_check    # differential vs Python model
```

## Measured results

- **81 scenario checks**: golden-book semantics (price-time priority,
  maker-price execution, partial fills, market-order handling, unknown-id
  cancels), hand-built streams with trades asserted at their exact cycles,
  and 25 randomized streams checked end to end.
- **Randomized sweep**: 50 deterministic streams × 5,000 messages =
  **250,000 messages**, producing **79,329 trades** — every trade matched
  the production engine field-by-field and landed on its exact predicted
  cycle. Zero divergences.
- **Differential test**: an independent naive Python model, which stamps
  trades purely from the pipeline specification (op index + 2), agrees
  exactly on a 20,000-operation stream — trade log with cycle stamps,
  final level quantities, and live count. Three implementations, one
  behavior.

## How it works

| Piece | File |
|---|---|
| Golden book | `include/chk/golden_book.hpp` — one vector of resting orders in arrival order, linear scans only; too simple to share a bug with the optimized engine |
| Pipeline | `include/chk/pipeline.hpp` — fetch/decode/execute registers as `std::optional` slots; time advances only when `step()` is called |
| Scoreboard | `src/scoreboard.cpp` — runs both implementations, attributes every engine trade to its message, and checks contents, cycle stamps, and final state |
| Stream generator | `include/chk/workload.hpp` — splitmix64, fixed seeds: resting adds, crossing adds, market orders, cancels |

The engine under check is vendored under `third_party/lob/` from the
author's limit-order-book repo so this project builds standalone. See
`DESIGN.md` for why cycle accuracy is a stronger property than output
equality, and where the model's timing claims begin and end.
