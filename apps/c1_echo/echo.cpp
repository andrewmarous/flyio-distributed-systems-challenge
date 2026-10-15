#include <iostream>

#include "lib/message.hpp"
#include "lib/node.hpp"

int main() {
    maelstrom::Node node;
    maelstrom::node_add_handler(
        node, "echo",
        [](maelstrom::Node &self,
           const maelstrom::Message &req) -> maelstrom::Result {
            return maelstrom::node_reply(
                self, req,
                Json{{"type", "echo_ok"}, {"echo", req.body.at("echo")}});
        });

    auto result = maelstrom::node_run(node);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    return 0;
}
