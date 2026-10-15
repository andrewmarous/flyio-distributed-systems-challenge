#include "lib/node.hpp"

#include <sstream>
#include <stdexcept>

namespace {
    void require(bool condition) {
        if (!condition) {
            throw std::runtime_error("node test failed");
        }
    }

    struct Output : std::stringbuf {
        int flushes = 0;
        int sync() override {
            ++flushes;
            return std::stringbuf::sync();
        }
    };

    const Json init = {
        {"src", "c1"}, {"dest", "n1"},
        {"body", {{"type", "init"}, {"msg_id", 1},
                  {"node_id", "n1"}, {"node_ids", {"n1", "n2"}}}},
    };
}

int main() {
    const Json echo = {
        {"src", "c2"}, {"dest", "n1"},
        {"body", {{"type", "echo"}, {"msg_id", 42}, {"echo", "hello"}}},
    };
    std::istringstream input(init.dump() + "\n" + echo.dump() + "\n");
    Output buffer;
    std::ostream output(&buffer);
    maelstrom::Node node(input, output);
    maelstrom::node_add_handler(node, "echo", [](auto& self, const auto& request) {
        return maelstrom::node_reply(self, request,
            Json{{"type", "echo_ok"}, {"echo", request.body.at("echo")}});
    });
    require(maelstrom::node_run(node).has_value());
    require(node.id == "n1");
    require(node.node_ids == std::vector<std::string>({"n1", "n2"}));
    require(node.neighbors.empty());
    require(buffer.flushes == 2);

    std::istringstream replies(buffer.str());
    auto first = maelstrom::node_recv(replies);
    auto second = maelstrom::node_recv(replies);
    require(first.has_value() && first->has_value());
    require(second.has_value() && second->has_value());
    require((**first).src == "n1" && (**first).dest == "c1");
    require((**first).body.at("type") == "init_ok");
    require((**first).body.at("in_reply_to") == 1);
    require((**second).src == "n1" && (**second).dest == "c2");
    require((**second).body.at("type") == "echo_ok");
    require((**second).body.at("echo") == "hello");
    require((**second).body.at("in_reply_to") == 42);
    require((**first).body.at("msg_id") != (**second).body.at("msg_id"));
    auto eof = maelstrom::node_recv(replies);
    require(eof.has_value() && !eof->has_value());

    std::istringstream malformed("not JSON\n");
    require(!maelstrom::node_recv(malformed).has_value());
    std::istringstream missing_fields("{}\n");
    require(!maelstrom::node_recv(missing_fields).has_value());
    std::istringstream broken;
    broken.setstate(std::ios::badbit);
    require(!maelstrom::node_recv(broken).has_value());

    const auto before = buffer.str();
    require(!maelstrom::node_reply(node, {"c1", "n1", Json::object()},
                                  Json{{"type", "echo_ok"}}).has_value());
    require(buffer.str() == before);
    require(!maelstrom::node_send(node, "n2", Json::array()).has_value());

    std::istringstream empty;
    std::ostringstream sink;
    maelstrom::Node uninitialized(empty, sink);
    require(!maelstrom::node_send(uninitialized, "n2", Json::object()).has_value());

    std::istringstream premature(echo.dump() + "\n");
    maelstrom::Node early(premature, sink);
    require(!maelstrom::node_run(early).has_value());

    std::istringstream unknown(init.dump() + "\n" + echo.dump() + "\n");
    maelstrom::Node no_handler(unknown, sink);
    require(!maelstrom::node_run(no_handler).has_value());

    std::istringstream failed_input;
    std::ostringstream failed_output;
    failed_output.setstate(std::ios::badbit);
    maelstrom::Node unwritable(failed_input, failed_output);
    unwritable.initialized = true;
    unwritable.id = "n1";
    require(!maelstrom::node_send(unwritable, "n2", Json::object()).has_value());
}
