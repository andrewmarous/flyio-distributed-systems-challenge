#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "message.hpp"

namespace maelstrom {
    using Result = std::expected<void, std::string>;

    struct Node;
    using Handler = std::function<Result(Node&, const Message&)>;

    struct Node {
        // The streams must outlive the node. Defaults connect to Maelstrom.
        explicit Node(std::istream& input = std::cin,
                      std::ostream& output = std::cout)
            : input(input), output(output) {}

        // Zero-based position in node_ids; valid after initialization.
        std::uint64_t pid = 0;
        std::string id_string;
        std::vector<std::string> node_ids; // Full membership, including this node.
        std::vector<std::string> neighbors; // Workload-defined topology.
        std::uint64_t seqnum = 1;
        bool initialized = false;
        std::unordered_map<std::string, Handler> handlers;
        std::istream& input;
        std::ostream& output;

        std::vector<int> bcast_messages;
    };

    void node_add_handler(Node& self, std::string type, Handler handler);

    // Allocate a fresh msg_id on every send; body is a copy owned by this call.
    Result node_send(Node& self, const std::string& dest, Json body);
    Result node_reply(Node& self, const Message& request, Json body);

    // Empty optional means normal EOF. Invalid input returns an error.
    std::expected<std::optional<Message>, std::string>
    node_recv(std::istream& input);

    // Handles init internally, then dispatches by body.type until EOF.
    // Input, dispatch, handler, and output errors stop the loop.
    Result node_run(Node& self);
}
