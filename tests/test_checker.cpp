// Scenario checks for the cycle-accurate golden-model checker: golden-book
// semantics, exact emission cycles on hand-built streams, scoreboard
// equivalence on targeted scenarios, and randomized stream equivalence.
#include <cstdio>
#include <vector>

#include "chk/golden_book.hpp"
#include "chk/pipeline.hpp"
#include "chk/workload.hpp"

using namespace lob;
using namespace chk;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                     \
    do {                                                \
        ++checks;                                       \
        if (!(cond)) {                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__,   \
                        __LINE__, #cond);               \
            ++failures;                                 \
        }                                               \
    } while (0)

static Message add(OrderId id, int side, int otype, Price price, Quantity qty) {
    Message m{};
    m.type = 'A';
    m.side = (std::uint8_t)side;
    m.order_type = (std::uint8_t)otype;
    m.id = id;
    m.price = price;
    m.quantity = qty;
    return m;
}

static Message cancel(OrderId id) {
    Message m{};
    m.type = 'C';
    m.id = id;
    return m;
}

static void test_golden_book_basics() {
    GoldenBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10050, 30, trades);
    CHECK(trades.empty());
    CHECK(book.best_ask() == 10050);
    CHECK(book.best_bid() == 0);
    book.add_order(2, Side::Buy, OrderType::Limit, 10100, 50, trades);
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 10050);  // maker's price
    CHECK(trades[0].quantity == 30);
    CHECK(book.quantity_at(Side::Buy, 10100) == 20);  // remainder rests
    CHECK(book.live_order_count() == 1);
    CHECK(!book.cancel_order(999));  // unknown id: no-op, reports false
    CHECK(book.cancel_order(2));
    CHECK(book.live_order_count() == 0);
    // Market order into an empty book: remainder is cancelled, nothing rests.
    book.add_order(3, Side::Buy, OrderType::Market, 0, 10, trades);
    CHECK(book.live_order_count() == 0);
    CHECK(trades.size() == 1);
}

static void test_time_priority() {
    // Two asks at the same price: the older one fills first.
    GoldenBook book;
    std::vector<Trade> trades;
    book.add_order(1, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(2, Side::Sell, OrderType::Limit, 10000, 10, trades);
    book.add_order(3, Side::Buy, OrderType::Limit, 10000, 15, trades);
    CHECK(trades.size() == 2);
    CHECK(trades[0].maker_id == 1 && trades[0].quantity == 10);
    CHECK(trades[1].maker_id == 2 && trades[1].quantity == 5);
}

static void test_exact_emission_cycle() {
    // Sell injected at cycle 0 (executes cycle 2, rests). Crossing buy
    // injected at cycle 1 must emit its trade at exactly cycle 3 -- not
    // cycle 2, not cycle 4.
    GoldenPipeline pipe;
    std::vector<StampedTrade> out;
    pipe.inject(add(1, 1, 0, 10000, 10));
    pipe.step(out);
    CHECK(out.empty());
    pipe.inject(add(2, 0, 0, 10000, 10));
    pipe.step(out);
    CHECK(out.empty());  // buy is only in the decode register so far
    pipe.step(out);
    CHECK(out.empty());  // the sell executes this cycle and rests: no trade
    pipe.step(out);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(out[0].cycle == 3);
        CHECK(out[0].trade.maker_id == 1 && out[0].trade.taker_id == 2);
    }
    while (!pipe.empty()) pipe.step(out);
    CHECK(out.size() == 1);  // nothing further, ever
}

static void test_scoreboard_scenarios() {
    std::uint64_t compared = 0;
    std::string error;
    {  // partial fill sweeping three levels
        std::vector<Message> s = {
            add(1, 1, 0, 10000, 10), add(2, 1, 0, 10001, 10),
            add(3, 1, 0, 10002, 10), add(4, 0, 0, 10005, 25),
        };
        CHECK(Scoreboard::run(s, 90000, 110000, compared, error));
        CHECK(compared == 3);
    }
    {  // cancel removes the only bid, then a sell rests unmatched
        std::vector<Message> s = {
            add(1, 0, 0, 9990, 10), cancel(1), add(2, 1, 0, 9995, 5),
        };
        CHECK(Scoreboard::run(s, 90000, 110000, compared, error));
        CHECK(compared == 0);
    }
    {  // market order sweeps the ask ladder
        std::vector<Message> s = {
            add(1, 1, 0, 10000, 10), add(2, 1, 0, 10003, 10),
            add(3, 0, 1, 0, 15),
        };
        CHECK(Scoreboard::run(s, 90000, 110000, compared, error));
        CHECK(compared == 2);
    }
    {  // cancel of unknown id mid-stream changes nothing
        std::vector<Message> s = {
            add(1, 1, 0, 10000, 10), cancel(777), add(2, 0, 0, 10000, 10),
        };
        CHECK(Scoreboard::run(s, 90000, 110000, compared, error));
        CHECK(compared == 1);
    }
}

static void test_randomized_streams() {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        StreamGen gen(seed * 1000 + 17, 100000);
        std::vector<Message> stream;
        for (int i = 0; i < 2000; ++i) stream.push_back(gen.next());
        std::uint64_t compared = 0;
        std::string error;
        bool ok = Scoreboard::run(stream, 90000, 110000, compared, error);
        if (!ok) std::printf("seed %llu: %s\n", (unsigned long long)seed, error.c_str());
        CHECK(ok);
        CHECK(compared > 0);
    }
}

int main() {
    test_golden_book_basics();
    test_time_priority();
    test_exact_emission_cycle();
    test_scoreboard_scenarios();
    test_randomized_streams();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
