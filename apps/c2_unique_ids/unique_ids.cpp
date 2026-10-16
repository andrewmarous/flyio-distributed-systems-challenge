#include <iostream>

#include "lib/message.hpp"
#include "lib/node.hpp"

#pragma once

int main() {
    maelstrom::Node node;
    maelstrom::node_add_handler(
        node, "generate",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            return maelstrom::node_reply(
                self, req,
                Json{{"type", "generate_ok"}, {"id", self.next_msg_id++}});
        });

    auto result = maelstrom::node_run(node);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    return 0;
}
