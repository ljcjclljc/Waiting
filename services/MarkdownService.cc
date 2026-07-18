#include "MarkdownService.h"

#include <md4c-html.h>
#include <md4c.h>

#include <stdexcept>

namespace blog
{
namespace
{
void appendOutput(const MD_CHAR *text, MD_SIZE size, void *userData)
{
    auto *output = static_cast<std::string *>(userData);
    output->append(text, size);
}
}  // namespace

std::string MarkdownService::render(const std::string &markdown)
{
    std::string output;
    const unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    const unsigned rendererFlags = MD_HTML_FLAG_SKIP_UTF8_BOM;
    const int result = md_html(markdown.data(),
                               static_cast<MD_SIZE>(markdown.size()),
                               appendOutput,
                               &output,
                               parserFlags,
                               rendererFlags);
    if (result != 0)
        throw std::runtime_error("Markdown rendering failed");

    const std::string externalLink = "<a href=\"http";
    const std::string safeExternalLink =
        "<a rel=\"nofollow noopener noreferrer\" href=\"http";
    std::size_t offset = 0;
    while ((offset = output.find(externalLink, offset)) != std::string::npos)
    {
        output.replace(offset, externalLink.size(), safeExternalLink);
        offset += safeExternalLink.size();
    }
    return output;
}
}  // namespace blog
