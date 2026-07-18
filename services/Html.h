#pragma once

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

namespace blog::html
{
inline std::string escape(const std::string &input)
{
    std::string output;
    output.reserve(input.size());
    for (const char ch : input)
    {
        switch (ch)
        {
            case '&':
                output += "&amp;";
                break;
            case '<':
                output += "&lt;";
                break;
            case '>':
                output += "&gt;";
                break;
            case '"':
                output += "&quot;";
                break;
            case '\'':
                output += "&#39;";
                break;
            default:
                output += ch;
        }
    }
    return output;
}

inline std::string attr(const Json::Value &value)
{
    return escape(value.isString() ? value.asString() : std::string{});
}

inline std::string text(const Json::Value &value)
{
    return escape(value.isString() ? value.asString() : std::string{});
}

inline std::string excerpt(const std::string &input, std::size_t maxLength)
{
    if (input.size() <= maxLength)
        return input;
    return input.substr(0, maxLength) + "...";
}

inline std::string urlEncode(const std::string &value)
{
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;
    for (const unsigned char ch : value)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            encoded << ch;
        }
        else
        {
            encoded << '%' << std::uppercase << std::setw(2)
                    << static_cast<int>(ch) << std::nouppercase;
        }
    }
    return encoded.str();
}
}  // namespace blog::html
