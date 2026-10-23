#pragma once

#include "lib/node.hpp"

using u64 = std::uint64_t;
using Clock = std::chrono::steady_clock;

struct Timer {
    u64 id;
    Clock::time_point deadline;
    std::function<maelstrom::Result(maelstrom::Node&)> callback;
};
