#include <array>
#include <iostream>

#include "lib/message.hpp"
#include "lib/node.hpp"

#pragma once

struct UniqueID {
    std::uint64_t seqnr;
    std::uint64_t pid;
};

using id = std::array<std::uint64_t, 2>;

int main() {
    maelstrom::Node node;
    maelstrom::node_add_handler(
        node, "generate",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            return maelstrom::node_reply(
                self, req,
                Json{{"type", "generate_ok"}, {"id", id{self.pid, self.seqnum}}});
        });

    auto result = maelstrom::node_run(node);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    return 0;
}
