#include "node.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <format>
#include <limits>
#include <poll.h>
#include <unistd.h>
#include <utility>
#include <variant>

namespace msutils {
    template <typename F> struct Defer {
        F cleanup;

        explicit Defer(F fn) : cleanup(std::move(fn)) {};

        Defer(const Defer &) = delete;
        Defer &operator=(const Defer &) = delete;

        ~Defer() noexcept { cleanup(); }
    };
} // namespace msutils

namespace maelstrom {
    void node_add_handler(Node &self, std::string type, Handler handler) {
        self.handlers.insert_or_assign(std::move(type), std::move(handler));
    }

    Result node_send(Node &self, const std::string &dest, Json body) {
        if (!self.initialized)
            return std::unexpected("cannot send before initialization");
        if (!body.is_object())
            return std::unexpected("message body must be an object");
        if (self.seqnum == std::numeric_limits<std::uint64_t>::max())
            return std::unexpected("message ID counter exhausted");

        try {
            body["msg_id"] = self.seqnum++;
            const Json envelope =
                Message{self.id_string, dest, std::move(body)};
            self.output << envelope.dump() << '\n' << std::flush;
            if (!self.output)
                return std::unexpected("failed to write message");

            return {};
        } catch (const Json::exception &error) {
            return std::unexpected(std::string{error.what()});
        } catch (const std::ios_base::failure &error) {
            return std::unexpected(std::string{error.what()});
        }
    }

    Result node_reply(Node &self, const Message &request, Json body) {
        try {
            const auto &id = request.body.at("msg_id");
            if (!id.is_number_integer()) {
                return std::unexpected("request msg_id must be an integer");
            }
            if (!body.is_object()) {
                return std::unexpected("reply body must be an object");
            }
            body["in_reply_to"] = id;
            return node_send(self, request.src, std::move(body));
        } catch (const Json::exception &error) {
            return std::unexpected(std::string{error.what()});
        }
    }

    std::expected<std::optional<Message>, std::string>
    node_recv(std::istream &input) {
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
        } catch (const Json::exception &error) {
            return std::unexpected(std::string{error.what()});
        } catch (const std::ios_base::failure &error) {
            if (input.eof() && !input.bad())
                return std::nullopt;
            return std::unexpected(std::string{error.what()});
        }
    }

    // used for nonblocking node_recv
    struct NeedsAddlReads {};
    struct EndOfInput {};

    using ReceiveEvent = std::variant<Message, NeedsAddlReads, EndOfInput>;
    using ReceiveResult = std::expected<ReceiveEvent, std::string>;

    struct MessageReceiver {
        std::string buffer;
        int fd = STDIN_FILENO;
        bool eof = false;
    };

    // extract a finished line from the buffer
    inline ReceiveResult extract_msg(MessageReceiver &receiver,
                                     std::size_t index) {
        std::string line = receiver.buffer.substr(0, index);
        receiver.buffer.erase(0, index + 1);
        auto msg = msg_deserialize(Json::parse(line));
        if (!msg)
            return std::unexpected(
                std::format("Error extracting message: {}", msg.error()));

        return ReceiveEvent{std::move(*msg)};
    }

    ReceiveResult try_node_recv(MessageReceiver &receiver) {
        try {
            // parse & return message if one exists
            auto newline_index = receiver.buffer.find("\n");
            if (newline_index != std::string::npos)
                return extract_msg(receiver, newline_index);

            // if hit eof, return end of input
            // NOTE: EOF only returned when the stream is closed
            if (receiver.eof) {
                if (!receiver.buffer.empty())
                    return std::unexpected(
                        std::format("input ended with unfinished message: {}",
                                    receiver.buffer));

                return EndOfInput{};
            }

            char cbuf[4096];
            const auto count = ::read(receiver.fd, cbuf, sizeof(cbuf));
            // successful read, append to buffer
            if (count > 0) {
                receiver.buffer.append(cbuf, static_cast<size_t>(count));
                newline_index = receiver.buffer.find("\n");

                if (newline_index != std::string::npos)
                    return extract_msg(receiver, newline_index);

                return NeedsAddlReads{};
            }

            // errored read, catch expected errors
            if (count < 0) {
                // NOTE: EWOULDBLOCK means stdin is empty
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    return NeedsAddlReads{};

                return std::unexpected("unexpected read() error");
            }

            // reached EOF, mark in reciever struct. Will be handled next
            // iteration
            receiver.eof = true;
            if (!receiver.buffer.empty())
                return std::unexpected(
                    std::format("input ended with unfinished message: {}",
                                receiver.buffer));

            return EndOfInput{};
        } catch (const Json::exception &error) {
            return std::unexpected(std::string{error.what()});
        } catch (const std::ios_base::failure &error) {
            return std::unexpected(std::string{error.what()});
        }
    }

    Result node_run(Node &self) {
        // set up nonblocking IO
        MessageReceiver receiver;
        int original = 0;
        if (self.input_fd) {
            receiver.fd = *self.input_fd;
            original = ::fcntl(receiver.fd, F_GETFL);
            if (original == -1)
                return std::unexpected("node_run: failed to get input flags");
            if (::fcntl(receiver.fd, F_SETFL, original | O_NONBLOCK) == -1)
                return std::unexpected("node_run: failed to set O_NONBLOCK");
        }

        auto restore_flags = msutils::Defer([fd = self.input_fd, original] {
            if (fd)
                ::fcntl(*fd, F_SETFL, original);
        });

        while (true) {
            // check & handle timers
            // try recv new message
            auto recv_result = [&]() -> ReceiveResult {
                if (self.input_fd)
                    return try_node_recv(receiver);
                auto received = node_recv(self.input);
                if (!received)
                    return std::unexpected(received.error());
                if (!received->has_value())
                    return EndOfInput{};
                return ReceiveEvent{std::move(**received)};
            }();
            if (!recv_result)
                return std::unexpected(recv_result.error());

            // if message: run callback
            auto &event = *recv_result;
            if (auto msg = std::get_if<maelstrom::Message>(&event)) {
                try {
                    const auto type = msg->body.at("type").get<std::string>();
                    if (type == "init") {
                        if (self.initialized)
                            return std::unexpected("duplicate init message");

                        auto id = msg->body.at("node_id").get<std::string>();
                        auto ids = msg->body.at("node_ids")
                                       .get<std::vector<std::string>>();
                        const auto position =
                            std::find(ids.begin(), ids.end(), id);
                        if (position == ids.end())
                            return std::unexpected(
                                "node_id is missing from node_ids");

                        auto sorted_ids = ids;
                        std::sort(sorted_ids.begin(), sorted_ids.end());
                        if (std::adjacent_find(sorted_ids.begin(),
                                               sorted_ids.end()) !=
                            sorted_ids.end())
                            return std::unexpected(
                                "node_ids contains duplicate IDs");

                        self.pid =
                            static_cast<std::uint64_t>(position - ids.begin());
                        self.id_string = std::move(id);
                        self.node_ids = std::move(ids);
                        self.initialized = true;

                        auto result =
                            node_reply(self, *msg, {{"type", "init_ok"}});
                        if (!result)
                            return result;
                        continue;
                    }

                    if (!self.initialized)
                        return std::unexpected("received message before init");

                    const auto handler = self.handlers.find(type);
                    if (handler == self.handlers.end())
                        return std::unexpected("no handler for message type: " +
                                               type);

                    const auto callback = handler->second;
                    auto result = callback(self, *msg);
                    if (!result)
                        return result;
                } catch (const Json::exception &error) {
                    return std::unexpected(std::string{error.what()});
                }
            }
            // if needs addl reads, poll() until input or next deadline
            else if (std::holds_alternative<maelstrom::NeedsAddlReads>(event)) {
                pollfd input{.fd = receiver.fd, .events = POLLIN, .revents = 0};

                // TODO: calculate this from the timer deadline
                int timeout_ms = -1;

                const int ready = ::poll(&input, 1, timeout_ms);

                if (ready < 0) {
                    if (errno == EINTR)
                        continue;

                    return std::unexpected(
                        std::format("poll failed: {}", std::strerror(errno)));
                }

                if (ready == 0)
                    continue; // poll timeout

                if (input.revents & POLLNVAL)
                    return std::unexpected("poll: invalid input descriptor");

                if (input.revents & POLLERR)
                    return std::unexpected("poll: input error");

                if (input.revents & (POLLIN | POLLHUP))
                    continue; // let next iter recv

            }
            // if end of input, stop
            else if (std::holds_alternative<maelstrom::EndOfInput>(event)) {
                return {};
            }
        }
    }
} // namespace maelstrom
