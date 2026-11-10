#include <cstdio>

#include "chk/pipeline.hpp"
#include "lob/feed.hpp"

namespace chk {

bool Scoreboard::run(const std::vector<lob::Message>& stream, lob::Price probe_lo,
                     lob::Price probe_hi, std::uint64_t& trades_compared,
                     std::string& error) {
    trades_compared = 0;

    // (a) Production engine, combinationally, remembering which message
    // produced each trade.
    lob::OrderBook dut;
    lob::FeedHandler feed(dut);
    std::vector<lob::Trade> dut_trades;
    std::vector<std::uint64_t> dut_trade_msg;  // stream index per trade
    for (std::size_t i = 0; i < stream.size(); ++i) {
        std::size_t before = dut_trades.size();
        feed.on_message(stream[i], dut_trades);
        for (std::size_t j = before; j < dut_trades.size(); ++j) {
            dut_trade_msg.push_back(i);
        }
    }

    // (b) Golden pipeline, one message per cycle, then drain.
    GoldenPipeline pipe;
    std::vector<StampedTrade> stamped;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        if (pipe.cycle() != i) {
            error = "internal: pipeline cycle drifted from stream index";
            return false;
        }
        pipe.inject(stream[i]);
        pipe.step(stamped);
    }
    while (!pipe.empty()) pipe.step(stamped);

    // Trade-by-trade equivalence, including the exact emission cycle.
    if (stamped.size() != dut_trades.size()) {
        char buf[128];
        std::snprintf(buf, sizeof buf,
                      "trade count differs: golden=%zu engine=%zu",
                      stamped.size(), dut_trades.size());
        error = buf;
        return false;
    }
    for (std::size_t j = 0; j < stamped.size(); ++j) {
        const lob::Trade& g = stamped[j].trade;
        const lob::Trade& d = dut_trades[j];
        if (g.trade_id != d.trade_id || g.taker_id != d.taker_id ||
            g.maker_id != d.maker_id || g.price != d.price ||
            g.quantity != d.quantity) {
            char buf[256];
            std::snprintf(buf, sizeof buf,
                          "trade %zu differs: golden(taker=%llu maker=%llu "
                          "px=%lld qty=%lld) engine(taker=%llu maker=%llu "
                          "px=%lld qty=%lld)",
                          j, (unsigned long long)g.taker_id,
                          (unsigned long long)g.maker_id, (long long)g.price,
                          (long long)g.quantity, (unsigned long long)d.taker_id,
                          (unsigned long long)d.maker_id, (long long)d.price,
                          (long long)d.quantity);
            error = buf;
            return false;
        }
        std::uint64_t expected_cycle = dut_trade_msg[j] + GoldenPipeline::kLatency;
        if (stamped[j].cycle != expected_cycle) {
            char buf[160];
            std::snprintf(buf, sizeof buf,
                          "trade %zu emitted at cycle %llu, expected %llu",
                          j, (unsigned long long)stamped[j].cycle,
                          (unsigned long long)expected_cycle);
            error = buf;
            return false;
        }
        ++trades_compared;
    }

    // Final book equivalence over the probe range.
    const GoldenBook& gb = pipe.book();
    if (gb.live_order_count() != dut.live_order_count()) {
        error = "final live order count differs";
        return false;
    }
    if (gb.best_bid() != dut.best_bid() || gb.best_ask() != dut.best_ask()) {
        error = "final best bid/ask differs";
        return false;
    }
    for (lob::Price p = probe_lo; p <= probe_hi; ++p) {
        if (gb.quantity_at(lob::Side::Buy, p) != dut.quantity_at(lob::Side::Buy, p) ||
            gb.quantity_at(lob::Side::Sell, p) != dut.quantity_at(lob::Side::Sell, p)) {
            char buf[128];
            std::snprintf(buf, sizeof buf, "final quantity differs at price %lld",
                          (long long)p);
            error = buf;
            return false;
        }
    }
    return true;
}

}  // namespace chk
