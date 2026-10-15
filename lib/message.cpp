#include "message.hpp"

#include <utility>

namespace maelstrom {
    namespace {
        void validate_body(const Json& body) {
            if (!body.is_object()) {
                throw Json::type_error::create(
                    302, "message body must be an object", &body);
            }
        }
    }

    void to_json(Json& json, const Message& message) {
        validate_body(message.body);
        json = Json{
            {"src", message.src},
            {"dest", message.dest},
            {"body", message.body},
        };
    }

    void from_json(const Json& json, Message& message) {
        // at() rejects missing fields; get() checks their types.
        // Construct a temporary so invalid input leaves message unchanged.
        Message parsed{
            json.at("src").get<std::string>(),
            json.at("dest").get<std::string>(),
            json.at("body"),
        };
        validate_body(parsed.body);
        message = std::move(parsed);
    }

    std::expected<Message, std::string> msg_deserialize(const Json& json) {
        try {
            return json.get<Message>();
        } catch (const Json::exception& error) {
            return std::unexpected(std::string{error.what()});
        }
    }

    std::expected<Json, std::string> msg_serialize(const Message& message) {
        try {
            return Json(message);
        } catch (const Json::exception& error) {
            return std::unexpected(std::string{error.what()});
        }
    }
}
