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
        node, "broadcast",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            self.bcast_messages.push_back(req.body["message"]);
            return maelstrom::node_reply(self, req, {{"type", "broadcast_ok"}});
        });

    maelstrom::node_add_handler(
        node, "read",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            return maelstrom::node_reply(
                self, req,
                {{"type", "read_ok"}, {"messages", self.bcast_messages}});
        });

    maelstrom::node_add_handler(
        node, "topology",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            self.neighbors = req.body.at("topology").at(self.id_string);
            return maelstrom::node_reply(self, req, {{"type", "topology_ok"}});
        });

    auto result = maelstrom::node_run(node);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    return 0;
}
