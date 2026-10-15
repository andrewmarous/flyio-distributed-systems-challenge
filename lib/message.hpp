#pragma once

#include <expected>
#include <string>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

namespace maelstrom {
    struct Message {
        std::string src;
        std::string dest;
        Json body;
    };

    // nlohmann::json finds these hooks through argument-dependent lookup.
    // They must be declared in the same namespace as Message.
    // Invalid envelopes throw Json::exception (including a non-object body).
    void to_json(Json& json, const Message& message);
    void from_json(const Json& json, Message& message);

    // Non-throwing alternatives for JSON conversion errors.
    std::expected<Message, std::string> msg_deserialize(const Json& json);
    std::expected<Json, std::string> msg_serialize(const Message& message);
}
