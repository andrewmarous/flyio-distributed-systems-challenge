#include <iostream>

#include "lib/message.hpp"
#include "lib/node.hpp"

#pragma once

int main() {
    maelstrom::Node node;

    // NOTE: don't need retries yet b/c network assumed reliable
    maelstrom::node_add_handler(
        node, "broadcast",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            int msg = req.body.at("message");
            if (self.bcast_messages.insert(msg).second) {
                for (const auto &other : self.neighbors) {
                    auto result = maelstrom::node_send(
                        self, other, {{"type", "broadcast"}, {"message", msg}});

                    if (!result)
                        std::cerr
                            << "Error relaying broadcast: " << result.error()
                            << "\n";
                }
            }

            return maelstrom::node_reply(self, req, {{"type", "broadcast_ok"}});
        });

    maelstrom::node_add_handler(
        node, "broadcast_ok",
        [](maelstrom::Node &self, const maelstrom::Message &req)
            -> maelstrom::Result { return maelstrom::Result{}; });

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
