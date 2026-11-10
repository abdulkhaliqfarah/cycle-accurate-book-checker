// Drives the scoreboard from a text operation stream and prints the golden
// pipeline's cycle-stamped trades and final state, for scripts/model.py to
// diff against a third, independent Python model.
//
// Input:
//   A <id> <B|S> <L|M> <price> <qty>
//   C <id>
//   END
//   Q <B|S> <price>
// Output:
//   T <cycle> <trade_id> <taker_id> <maker_id> <price> <qty>
//   Q <B|S> <price> <qty>
//   LIVE <n>
// Exit code 1 (with the reason on stderr) if the scoreboard finds any
// divergence between the golden pipeline and the production engine.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "chk/pipeline.hpp"
#include "lob/feed.hpp"

using namespace lob;
using namespace chk;

int main() {
    std::vector<Message> stream;
    struct Query {
        int side;
        Price price;
    };
    std::vector<Query> queries;
    char line[256];
    bool in_queries = false;
    while (std::fgets(line, sizeof line, stdin)) {
        if (line[0] == 'E') {
            in_queries = true;
            continue;
        }
        if (!in_queries && line[0] == 'A') {
            Message m{};
            char side_c, type_c;
            if (std::sscanf(line, "A %llu %c %c %lld %lld",
                            (unsigned long long*)&m.id, &side_c, &type_c,
                            (long long*)&m.price, (long long*)&m.quantity) != 5) {
                std::fprintf(stderr, "bad line: %s", line);
                return 2;
            }
            m.type = 'A';
            m.side = side_c == 'B' ? 0 : 1;
            m.order_type = type_c == 'L' ? 0 : 1;
            stream.push_back(m);
        } else if (!in_queries && line[0] == 'C') {
            Message m{};
            if (std::sscanf(line, "C %llu", (unsigned long long*)&m.id) != 1) {
                std::fprintf(stderr, "bad line: %s", line);
                return 2;
            }
            m.type = 'C';
            stream.push_back(m);
        } else if (in_queries && line[0] == 'Q') {
            Query q{};
            char side_c;
            if (std::sscanf(line, "Q %c %lld", &side_c, (long long*)&q.price) != 2) {
                std::fprintf(stderr, "bad line: %s", line);
                return 2;
            }
            q.side = side_c == 'B' ? 0 : 1;
            queries.push_back(q);
        }
    }
    if (stream.empty()) {
        std::fprintf(stderr, "empty stream\n");
        return 2;
    }

    std::uint64_t compared = 0;
    std::string error;
    if (!Scoreboard::run(stream, 90000, 110000, compared, error)) {
        std::fprintf(stderr, "SCOREBOARD DIVERGENCE: %s\n", error.c_str());
        return 1;
    }

    // Re-run the golden pipeline to emit the stamped trades and final state.
    GoldenPipeline pipe;
    std::vector<StampedTrade> stamped;
    for (const auto& m : stream) {
        pipe.inject(m);
        pipe.step(stamped);
    }
    while (!pipe.empty()) pipe.step(stamped);

    for (const auto& st : stamped) {
        std::printf("T %llu %llu %llu %llu %lld %lld\n",
                    (unsigned long long)st.cycle,
                    (unsigned long long)st.trade.trade_id,
                    (unsigned long long)st.trade.taker_id,
                    (unsigned long long)st.trade.maker_id,
                    (long long)st.trade.price, (long long)st.trade.quantity);
    }
    for (const Query& q : queries) {
        Quantity qty = pipe.book().quantity_at(q.side == 0 ? Side::Buy : Side::Sell,
                                               q.price);
        std::printf("Q %c %lld %lld\n", q.side == 0 ? 'B' : 'S',
                    (long long)q.price, (long long)qty);
    }
    std::printf("LIVE %zu\n", pipe.book().live_order_count());
    std::fprintf(stderr, "scoreboard: %llu trades compared, all cycles exact\n",
                 (unsigned long long)compared);
    return 0;
}
