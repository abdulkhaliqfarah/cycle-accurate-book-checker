#pragma once

#include <cstdint>
#include <vector>

#include "lob/order_book.hpp"

namespace chk {

// The golden model: a deliberately naive limit order book used as the
// reference in cycle-accurate checking. Resting orders live in one vector in
// arrival order; matching is a linear scan for the best opposing price with
// ties broken by arrival order. There is no index, no pool, no optimization
// of any kind -- the whole point is that it is too simple to share a bug
// with the optimized engine it checks.
//
// Semantics are exactly the engine's: strict price-time priority, execution
// at the maker's price, partial fills, market-order remainders cancelled,
// cancel of an unknown id is a no-op.
class GoldenBook {
public:
    void add_order(lob::OrderId id, lob::Side side, lob::OrderType type,
                   lob::Price price, lob::Quantity qty,
                   std::vector<lob::Trade>& trades) {
        lob::Quantity remaining = qty;
        while (remaining > 0) {
            int best = best_opposing_index(side);
            if (best < 0) break;
            const Resting& maker = resting_[best];
            if (type == lob::OrderType::Limit) {
                bool crosses = (side == lob::Side::Buy) ? (maker.price <= price)
                                                        : (maker.price >= price);
                if (!crosses) break;
            }
            lob::Quantity fill = remaining < maker.qty ? remaining : maker.qty;
            ++trade_seq_;
            trades.push_back(lob::Trade{trade_seq_, id, maker.id, maker.price, fill});
            remaining -= fill;
            resting_[best].qty -= fill;
            if (resting_[best].qty == 0) {
                resting_.erase(resting_.begin() + best);
            }
        }
        if (remaining > 0 && type == lob::OrderType::Limit) {
            resting_.push_back(Resting{id, side, price, remaining});
        }
    }

    bool cancel_order(lob::OrderId id) {
        for (std::size_t i = 0; i < resting_.size(); ++i) {
            if (resting_[i].id == id) {
                resting_.erase(resting_.begin() + static_cast<long>(i));
                return true;
            }
        }
        return false;
    }

    lob::Price best_bid() const { return best_price(lob::Side::Buy); }
    lob::Price best_ask() const { return best_price(lob::Side::Sell); }

    lob::Quantity quantity_at(lob::Side side, lob::Price price) const {
        lob::Quantity total = 0;
        for (const auto& o : resting_) {
            if (o.side == side && o.price == price) total += o.qty;
        }
        return total;
    }

    std::size_t live_order_count() const { return resting_.size(); }
    std::uint64_t next_trade_id() const { return trade_seq_; }

private:
    struct Resting {
        lob::OrderId id;
        lob::Side side;
        lob::Price price;
        lob::Quantity qty;
    };

    // Index of the best opposing order for an incoming `side`, or -1. The
    // vector is in arrival order, so the first index attaining the best
    // price is also the time-priority winner.
    int best_opposing_index(lob::Side incoming) const {
        int best = -1;
        for (std::size_t i = 0; i < resting_.size(); ++i) {
            const Resting& o = resting_[i];
            if (o.side == incoming) continue;
            if (best < 0) {
                best = static_cast<int>(i);
            } else if (incoming == lob::Side::Buy &&
                       o.price < resting_[best].price) {
                best = static_cast<int>(i);  // buyer wants the lowest ask
            } else if (incoming == lob::Side::Sell &&
                       o.price > resting_[best].price) {
                best = static_cast<int>(i);  // seller wants the highest bid
            }
        }
        return best;
    }

    lob::Price best_price(lob::Side side) const {
        lob::Price best = 0;
        for (const auto& o : resting_) {
            if (o.side != side) continue;
            if (best == 0 || (side == lob::Side::Buy && o.price > best) ||
                (side == lob::Side::Sell && o.price < best)) {
                best = o.price;
            }
        }
        return best;
    }

    std::vector<Resting> resting_;
    std::uint64_t trade_seq_ = 0;
};

}  // namespace chk
