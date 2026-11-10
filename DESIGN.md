# Design Notes — Cycle-Accurate Order Book Checker

## Why cycle accuracy, not just output equality

A plain differential test proves two implementations produce the same
trades *eventually, in the same order*. A pipelined implementation — in
hardware or in a staged software feed handler — can pass that test while
being wrong in ways that matter: emitting a trade a cycle late (stale
data for anything downstream), or attributing a trade to the wrong input
message (a correctness bug that ordering alone can mask when two messages
produce similar trades). Cycle-accurate checking closes both holes by
predicting the exact emission cycle of every output and treating any
deviation as a failure.

## The pipeline model

Three stages, one message per cycle, fixed latency:

- **fetch** — the 32-byte feed message is latched into a register;
- **decode** — the message becomes a typed book command;
- **execute** — the command is applied to the golden book; resulting
  trades are emitted, stamped with the current cycle.

A message injected at cycle *c* is executed during cycle *c + 2*, so all
of its trades must be stamped *c + 2*. Registers are `std::optional`
slots in the `GoldenPipeline`; `step()` executes whatever sits in the
decode register, shifts fetch → decode and input → fetch, and increments
the cycle counter. There is no other source of time in the system, which
is what makes the check exact rather than statistical.

Honest limits of the model: real hardware pipelines stall, backpressure,
and handle multi-message bursts; this model has a fixed 2-cycle latency
and never stalls. It verifies the *checking discipline* and the golden
model's semantics, not a synthesizable design. The README says the same
thing in its scope note.

## The golden model

The golden book is intentionally the dumbest correct implementation: one
`std::vector` of resting orders in arrival order. Best opposing price is
found by linear scan; the first order attaining that price is the
time-priority winner, because the vector is in arrival order. Fills erase
or shrink vector entries. There is no id index, no price-level map, no
pool allocator — none of the machinery whose bugs this model exists to
catch in the production engine. Its semantics are the exchange rules and
nothing else: strict price-time priority, execution at the maker's price,
partial fills rest, market-order remainders are cancelled, cancelling an
unknown id is a no-op.

## The scoreboard

`Scoreboard::run` drives both implementations from the same stream:

1. The production engine runs combinationally, message by message, and
   the scoreboard records which stream index produced each trade.
2. The golden pipeline runs cycle by cycle; after the stream ends it is
   stepped until every register drains, so tail trades are checked too.
3. Comparison is threefold: trade sequences identical field-by-field;
   each golden trade's stamp equals its producing message's index + 2;
   final state identical — live count, best bid/ask, and resting quantity
   at every price in a probe range, so a state divergence that produced
   no trades is still caught.

First divergence stops the run with a specific message (which trade, what
differed, expected vs actual cycle), because a checker that only says
"mismatch somewhere in 250,000 messages" is not a debugging tool.

## Determinism

All streams come from a splitmix64 generator with fixed seeds, in C++ and
(separately) in the Python differential model. A failure reproduces
exactly, from the same command, forever — the property that makes the
sweep usable as a regression gate: `./build/sweep` either agrees on every
one of its quarter-million messages or prints the stream and trade where
it stopped agreeing.
