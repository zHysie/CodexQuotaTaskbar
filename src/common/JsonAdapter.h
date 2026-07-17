#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cqt
{

class JsonValue
{
public:
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

    JsonValue() = default;
    explicit JsonValue(Storage value) : value_(std::move(value)) {}
    ~JsonValue();
    JsonValue(const JsonValue&) = default;
    JsonValue& operator=(const JsonValue&) = default;
    JsonValue(JsonValue&&) noexcept = default;
    JsonValue& operator=(JsonValue&&) noexcept = default;

    [[nodiscard]] bool IsNull() const;
    [[nodiscard]] bool IsBool() const;
    [[nodiscard]] bool IsNumber() const;
    [[nodiscard]] bool IsString() const;
    [[nodiscard]] bool IsObject() const;
    [[nodiscard]] bool IsArray() const;
    [[nodiscard]] const bool* AsBool() const;
    [[nodiscard]] const double* AsNumber() const;
    [[nodiscard]] const std::string* AsString() const;
    [[nodiscard]] const Object* AsObject() const;
    [[nodiscard]] const Array* AsArray() const;
    [[nodiscard]] const JsonValue* Find(std::string_view key) const;

private:
    Storage value_ = nullptr;
};

struct JsonParseResult
{
    bool success = false;
    JsonValue root;
    std::string errorCode;
};

class JsonAdapter
{
public:
    [[nodiscard]] static JsonParseResult Parse(std::string_view text);
};

} // namespace cqt
