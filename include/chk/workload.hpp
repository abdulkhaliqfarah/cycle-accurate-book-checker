#pragma once

#include <cstdint>
#include <vector>

#include "lob/feed.hpp"

namespace chk {

// splitmix64, deterministic on every platform: same seed -> same stream.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next_u64() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::uint64_t below(std::uint64_t n) { return next_u64() % n; }

private:
    std::uint64_t state_;
};

// Deterministic message-stream generator for randomized checking: a mix of
// resting adds, crossing adds, market orders, and cancels of live ids.
class StreamGen {
public:
    StreamGen(std::uint64_t seed, lob::Price mid) : rng_(seed), mid_(mid) {}

    lob::Message next() {
        std::uint64_t r = rng_.below(100);
        if (r < 50 || live_.empty()) return resting();
        if (r < 75) return crossing();
        if (r < 80) return market();
        return cancel();
    }

private:
    lob::Message base(char type) {
        lob::Message m{};
        m.type = type;
        return m;
    }
    lob::Message resting() {
        lob::Message m = base('A');
        bool buy = (rng_.next_u64() & 1) != 0;
        lob::Price offset = 5 + static_cast<lob::Price>(rng_.below(20));
        m.side = buy ? 0 : 1;
        m.order_type = 0;
        m.price = buy ? mid_ - offset : mid_ + offset;
        m.quantity = 1 + static_cast<lob::Quantity>(rng_.below(100));
        m.id = next_id_++;
        live_.push_back(m.id);
        return m;
    }
    lob::Message crossing() {
        lob::Message m = base('A');
        bool buy = (rng_.next_u64() & 1) != 0;
        lob::Price through = 1 + static_cast<lob::Price>(rng_.below(5));
        m.side = buy ? 0 : 1;
        m.order_type = 0;
        m.price = buy ? mid_ + through : mid_ - through;
        m.quantity = 1 + static_cast<lob::Quantity>(rng_.below(50));
        m.id = next_id_++;
        return m;
    }
    lob::Message market() {
        lob::Message m = base('A');
        m.side = (rng_.next_u64() & 1) != 0 ? 1 : 0;
        m.order_type = 1;
        m.price = 0;
        m.quantity = 1 + static_cast<lob::Quantity>(rng_.below(50));
        m.id = next_id_++;
        return m;
    }
    lob::Message cancel() {
        lob::Message m = base('C');
        std::size_t idx = static_cast<std::size_t>(rng_.below(live_.size()));
        m.id = live_[idx];
        live_[idx] = live_.back();
        live_.pop_back();
        return m;
    }

    SplitMix64 rng_;
    lob::Price mid_;
    lob::OrderId next_id_ = 1;
    std::vector<lob::OrderId> live_;
};

}  // namespace chk
