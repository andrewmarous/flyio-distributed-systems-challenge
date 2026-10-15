#include "lib/message.hpp"

#include <stdexcept>

namespace {
    void require(bool condition) {
        if (!condition) {
            throw std::runtime_error("message conversion test failed");
        }
    }
}

int main() {
    const Json original = {
        {"src", "c1"},
        {"dest", "n1"},
        {"body", {{"type", "echo"}, {"echo", "hello"},
                  {"extra", Json::array({1, nullptr, "value"})}}},
    };

    // Direct conversions use the namespace-level hooks.
    const auto message = original.get<maelstrom::Message>();
    const Json serialized = message;
    require(serialized == original);
    require(!serialized.at("body").contains("msg_id"));

    // The expected-based wrappers use the same conversions.
    const auto decoded = maelstrom::msg_deserialize(original);
    require(decoded.has_value());
    const auto encoded = maelstrom::msg_serialize(*decoded);
    require(encoded.has_value() && *encoded == original);

    auto missing = original;
    missing.erase("src");
    require(!maelstrom::msg_deserialize(missing).has_value());

    auto wrong_type = original;
    wrong_type["src"] = 123;
    require(!maelstrom::msg_deserialize(wrong_type).has_value());

    for (const Json& invalid : {Json(nullptr), Json::array(), Json("text")}) {
        require(!maelstrom::msg_deserialize(invalid).has_value());
        auto invalid_body = original;
        invalid_body["body"] = invalid;
        require(!maelstrom::msg_deserialize(invalid_body).has_value());
        require(!maelstrom::msg_serialize({"n1", "c1", invalid}).has_value());
    }

    // Failed get_to() must not partially modify an existing message.
    auto unchanged = message;
    try {
        missing.get_to(unchanged);
        require(false);
    } catch (const Json::exception&) {
        require(Json(unchanged) == original);
    }
}
