#include "common/JsonAdapter.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <cmath>

namespace
{

cqt::JsonValue Convert(const nlohmann::json& source)
{
    if (source.is_null())
    {
        return cqt::JsonValue(nullptr);
    }
    if (source.is_boolean())
    {
        return cqt::JsonValue(source.get<bool>());
    }
    if (source.is_number())
    {
        return cqt::JsonValue(source.get<double>());
    }
    if (source.is_string())
    {
        return cqt::JsonValue(source.get<std::string>());
    }
    if (source.is_array())
    {
        cqt::JsonValue::Array array;
        array.reserve(source.size());
        for (const auto& item : source)
        {
            array.push_back(Convert(item));
        }
        return cqt::JsonValue(std::move(array));
    }

    cqt::JsonValue::Object object;
    for (auto iterator = source.begin(); iterator != source.end(); ++iterator)
    {
        object.emplace(iterator.key(), Convert(iterator.value()));
    }
    return cqt::JsonValue(std::move(object));
}

void SecureClear(nlohmann::json& value)
{
    if (value.is_string())
    {
        std::string& text = value.get_ref<std::string&>();
        if (!text.empty())
        {
            SecureZeroMemory(text.data(), text.size());
        }
    }
    else if (value.is_array() || value.is_object())
    {
        for (auto& item : value)
        {
            SecureClear(item);
        }
    }
}

} // namespace

namespace cqt
{

JsonValue::~JsonValue()
{
    if (std::string* text = std::get_if<std::string>(&value_); text && !text->empty())
    {
        SecureZeroMemory(text->data(), text->size());
    }
}

bool JsonValue::IsNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
bool JsonValue::IsBool() const { return std::holds_alternative<bool>(value_); }
bool JsonValue::IsNumber() const { return std::holds_alternative<double>(value_); }
bool JsonValue::IsString() const { return std::holds_alternative<std::string>(value_); }
bool JsonValue::IsObject() const { return std::holds_alternative<Object>(value_); }
bool JsonValue::IsArray() const { return std::holds_alternative<Array>(value_); }
const bool* JsonValue::AsBool() const { return std::get_if<bool>(&value_); }
const double* JsonValue::AsNumber() const { return std::get_if<double>(&value_); }
const std::string* JsonValue::AsString() const { return std::get_if<std::string>(&value_); }
const JsonValue::Object* JsonValue::AsObject() const { return std::get_if<Object>(&value_); }
const JsonValue::Array* JsonValue::AsArray() const { return std::get_if<Array>(&value_); }

const JsonValue* JsonValue::Find(std::string_view key) const
{
    const Object* object = AsObject();
    if (!object)
    {
        return nullptr;
    }
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

JsonParseResult JsonAdapter::Parse(std::string_view text)
{
    JsonParseResult result;
    try
    {
        nlohmann::json parsed = nlohmann::json::parse(text.begin(), text.end(), nullptr, true, true);
        result.root = Convert(parsed);
        SecureClear(parsed);
        result.success = true;
    }
    catch (const nlohmann::json::exception&)
    {
        result.errorCode = "JSON_INVALID";
    }
    return result;
}

} // namespace cqt
