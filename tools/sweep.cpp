// Randomized equivalence sweep: runs many deterministic streams through
// the scoreboard and reports totals. Usage: sweep [streams] [messages] [seed0]
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "chk/pipeline.hpp"
#include "chk/workload.hpp"

using namespace lob;
using namespace chk;

int main(int argc, char** argv) {
    int streams = argc > 1 ? std::atoi(argv[1]) : 50;
    int messages = argc > 2 ? std::atoi(argv[2]) : 5000;
    std::uint64_t seed0 = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 500000;
    std::uint64_t total_trades = 0;
    for (int s = 0; s < streams; ++s) {
        StreamGen gen(seed0 + s * 7919, 100000);
        std::vector<Message> stream;
        stream.reserve(messages);
        for (int i = 0; i < messages; ++i) stream.push_back(gen.next());
        std::uint64_t compared = 0;
        std::string error;
        if (!Scoreboard::run(stream, 90000, 110000, compared, error)) {
            std::printf("DIVERGENCE stream %d: %s\n", s, error.c_str());
            return 1;
        }
        total_trades += compared;
    }
    std::printf("AGREE: %d streams x %d messages, %llu trades compared, "
                "every emission cycle exact\n",
                streams, messages, (unsigned long long)total_trades);
    return 0;
}
