#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "chk/golden_book.hpp"
#include "lob/feed.hpp"

namespace chk {

// A trade emitted by the golden pipeline, stamped with the cycle at which it
// left the execute stage.
struct StampedTrade {
    std::uint64_t cycle;
    lob::Trade trade;
};

// A cycle-stepped model of a 3-stage hardware-style matching pipeline:
//
//   stage 1  fetch    the 32-byte feed message is latched into a register
//   stage 2  decode   the message is decoded into a book command
//   stage 3  execute  the command is applied to the golden book; any trades
//                     it produces are emitted, stamped with this cycle
//
// One message is injected per cycle. A message injected at cycle c has its
// trades emitted at cycle c + kLatency. The model advances only when step()
// is called -- there is no hidden concurrency -- which is what makes the
// checking cycle-accurate: the scoreboard can demand not just that the same
// trades occur, but that each one occurs at exactly the predicted cycle.
class GoldenPipeline {
public:
    static constexpr std::uint64_t kLatency = 2;  // inject at c -> emit at c+2

    // Latch a message into the input register. One per cycle.
    void inject(const lob::Message& msg) { pending_ = msg; }

    // Advance one cycle. Trades produced by the execute stage during this
    // cycle are appended to `out`, stamped with the current cycle.
    void step(std::vector<StampedTrade>& out) {
        if (decode_reg_.has_value()) {
            execute(*decode_reg_, out);
        }
        decode_reg_ = fetch_reg_;
        fetch_reg_ = pending_;
        pending_.reset();
        ++cycle_;
    }

    // True when every register has drained.
    bool empty() const {
        return !pending_ && !fetch_reg_ && !decode_reg_;
    }

    std::uint64_t cycle() const { return cycle_; }
    const GoldenBook& book() const { return book_; }

private:
    void execute(const lob::Message& msg, std::vector<StampedTrade>& out) {
        std::vector<lob::Trade> trades;
        if (msg.type == 'A') {
            book_.add_order(msg.id,
                            msg.side == 0 ? lob::Side::Buy : lob::Side::Sell,
                            msg.order_type == 0 ? lob::OrderType::Limit
                                                : lob::OrderType::Market,
                            msg.price, msg.quantity, trades);
        } else if (msg.type == 'C') {
            book_.cancel_order(msg.id);
        }
        for (const auto& t : trades) out.push_back(StampedTrade{cycle_, t});
    }

    GoldenBook book_;
    std::optional<lob::Message> pending_;     // at the input pin this cycle
    std::optional<lob::Message> fetch_reg_;   // stage 1 register
    std::optional<lob::Message> decode_reg_;  // stage 2 register
    std::uint64_t cycle_ = 0;
};

// The scoreboard. Runs a message stream through (a) the production engine,
// combinationally, in stream order, and (b) the golden pipeline, one message
// per cycle, and checks cycle-accurate equivalence:
//
//   * the trade sequences are identical, field by field;
//   * every trade from message i is stamped exactly cycle i + kLatency --
//     no trade early, none late, none from the wrong message;
//   * the final books agree: live count, best bid/ask, and the resting
//     quantity at every price in a probe range.
//
// Returns the number of trades compared. On any divergence it returns false
// and describes the first mismatch in `error`.
struct Scoreboard {
    static bool run(const std::vector<lob::Message>& stream, lob::Price probe_lo,
                    lob::Price probe_hi, std::uint64_t& trades_compared,
                    std::string& error);
};

}  // namespace chk
