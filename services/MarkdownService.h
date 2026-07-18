#pragma once

#include <string>

namespace blog
{
class MarkdownService
{
  public:
    static std::string render(const std::string &markdown);
};
}  // namespace blog
