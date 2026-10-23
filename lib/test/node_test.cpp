#include "lib/node.hpp"

#include <fcntl.h>
#include <unistd.h>
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
    require(node.id_string == "n1");
    require(node.pid == 0);
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

    auto check_membership = [](Json ids, std::string id,
                               bool valid, std::uint64_t expected_pid = 0) {
        auto message = init;
        message["body"]["node_ids"] = std::move(ids);
        message["body"]["node_id"] = std::move(id);
        std::istringstream input(message.dump() + "\n");
        std::ostringstream output;
        maelstrom::Node node(input, output);
        require(maelstrom::node_run(node).has_value() == valid);
        require(node.initialized == valid);
        if (valid) {
            require(node.pid == expected_pid);
        } else {
            require(output.str().empty());
        }
    };
    check_membership(Json{"alpha", "beta"}, "beta", true, 1);
    check_membership(Json{"alpha"}, "beta", false);
    check_membership(Json{"alpha", "alpha"}, "alpha", false);
    check_membership(Json::array(), "alpha", false);
    Json large_membership = Json::array();
    for (int i = 0; i < 256; ++i)
        large_membership.push_back("node-" + std::to_string(i));
    check_membership(large_membership, "node-255", true, 255);
    large_membership.push_back("node-256");
    check_membership(large_membership, "node-256", true, 256);

    std::istringstream malformed("not JSON\n");
    require(!maelstrom::node_recv(malformed).has_value());
    std::istringstream missing_fields("{}\n");
    require(!maelstrom::node_recv(missing_fields).has_value());
    std::istringstream broken;
    broken.setstate(std::ios::badbit);
    require(!maelstrom::node_recv(broken).has_value());

    // Exercise descriptor input, EOF, dispatch errors, and flag restoration.
    auto check_pipe = [&](const std::string& data, bool success,
                          bool initially_nonblocking = false) {
        int fds[2];
        require(::pipe(fds) == 0);
        if (initially_nonblocking)
            require(::fcntl(fds[0], F_SETFL, O_NONBLOCK) != -1);
        const int original = ::fcntl(fds[0], F_GETFL);
        require(::write(fds[1], data.data(), data.size()) ==
                static_cast<ssize_t>(data.size()));
        require(::close(fds[1]) == 0);
        std::ostringstream output;
        maelstrom::Node node(fds[0], output);
        maelstrom::node_add_handler(node, "echo", [](auto& self, const auto& req) {
            // Replacing the executing callback must be safe.
            maelstrom::node_add_handler(self, "echo", [](auto&, const auto&) {
                return maelstrom::Result{};
            });
            return maelstrom::node_reply(self, req, {{"type", "echo_ok"}});
        });
        require(maelstrom::node_run(node).has_value() == success);
        require(::fcntl(fds[0], F_GETFL) == original);
        require(::close(fds[0]) == 0);
        return output.str();
    };
    const auto pipe_replies = check_pipe(init.dump() + "\n" + echo.dump() + "\n", true);
    require(pipe_replies.find("init_ok") != std::string::npos);
    require(pipe_replies.find("echo_ok") != std::string::npos);
    check_pipe("", true, true);
    check_pipe("{", false);
    check_pipe("not JSON\n", false);
    check_pipe(echo.dump() + "\n", false);
    check_pipe(init.dump() + "\n" + init.dump() + "\n", false);
    auto unknown_message = echo;
    unknown_message["body"]["type"] = "unknown";
    check_pipe(init.dump() + "\n" + unknown_message.dump() + "\n", false);
    auto missing_type = echo;
    missing_type["body"].erase("type");
    check_pipe(init.dump() + "\n" + missing_type.dump() + "\n", false);

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
    unwritable.id_string = "n1";
    require(!maelstrom::node_send(unwritable, "n2", Json::object()).has_value());
}
