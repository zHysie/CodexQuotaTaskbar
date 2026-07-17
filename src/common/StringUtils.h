#pragma once

#include <string>
#include <string_view>

namespace cqt
{

std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);

} // namespace cqt
