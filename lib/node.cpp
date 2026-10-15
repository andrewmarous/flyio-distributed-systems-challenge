#include "node.hpp"

#include <limits>
#include <utility>

namespace maelstrom {
    void node_add_handler(Node& self, std::string type, Handler handler) {
        self.handlers.insert_or_assign(std::move(type), std::move(handler));
    }

    Result node_send(Node& self, const std::string& dest, Json body) {
        if (!self.initialized)
            return std::unexpected("cannot send before initialization");
        if (!body.is_object())
            return std::unexpected("message body must be an object");
        if (self.next_msg_id == std::numeric_limits<std::int64_t>::max())
            return std::unexpected("message ID counter exhausted");

        try {
            body["msg_id"] = self.next_msg_id++;
            const Json envelope = Message{self.id, dest, std::move(body)};
            self.output << envelope.dump() << '\n' << std::flush;
            if (!self.output)
                return std::unexpected("failed to write message");

            return {};
        } catch (const Json::exception& error) {
            return std::unexpected(std::string{error.what()});
        } catch (const std::ios_base::failure& error) {
            return std::unexpected(std::string{error.what()});
        }
    }

    Result node_reply(Node& self, const Message& request, Json body) {
        try {
            const auto& id = request.body.at("msg_id");
            if (!id.is_number_integer()) {
                return std::unexpected("request msg_id must be an integer");
            }
            if (!body.is_object()) {
                return std::unexpected("reply body must be an object");
            }
            body["in_reply_to"] = id;
            return node_send(self, request.src, std::move(body));
        } catch (const Json::exception& error) {
            return std::unexpected(std::string{error.what()});
        }
    }

    std::expected<std::optional<Message>, std::string>
    node_recv(std::istream& input) {
        try {
            std::string line;
            if (!std::getline(input, line)) {
                if (input.eof() && !input.bad())
                    return std::nullopt;

                return std::unexpected("failed to read message");
            }

            auto message = msg_deserialize(Json::parse(line));
            if (!message)
                return std::unexpected(message.error());

            return std::optional<Message>{std::move(*message)};
        } catch (const Json::exception& error) {
            return std::unexpected(std::string{error.what()});
        } catch (const std::ios_base::failure& error) {
            if (input.eof() && !input.bad())
                return std::nullopt;

            return std::unexpected(std::string{error.what()});
        }
    }

    Result node_run(Node& self) {
        while (true) {
            auto received = node_recv(self.input);
            if (!received)
                return std::unexpected(received.error());

            if (!received->has_value())
                return {};

            const auto& request = **received;
            try {
                const auto type = request.body.at("type").get<std::string>();
                if (type == "init") {
                    if (self.initialized)
                        return std::unexpected("duplicate init message");

                    auto id = request.body.at("node_id").get<std::string>();
                    auto ids = request.body.at("node_ids")
                                   .get<std::vector<std::string>>();
                    self.id = std::move(id);
                    self.node_ids = std::move(ids);
                    self.initialized = true;
                    auto result = node_reply(self, request, Json{{"type", "init_ok"}});
                    if (!result)
                        return result;

                    continue;
                }
                if (!self.initialized)
                    return std::unexpected("received message before init");

                const auto handler = self.handlers.find(type);
                if (handler == self.handlers.end())
                    // Fail explicitly rather than automatically replying to a
                    // possible response or one-way message.
                    return std::unexpected("no handler for message type: " + type);

                // Copy the callback so handlers can safely register handlers.
                const auto callback = handler->second;
                auto result = callback(self, request);
                if (!result)
                    return result;

            } catch (const Json::exception& error) {
                return std::unexpected(std::string{error.what()});
            }
        }
    }
}
