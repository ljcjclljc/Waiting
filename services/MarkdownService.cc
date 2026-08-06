#include "MarkdownService.h"

#include <md4c-html.h>
#include <md4c.h>

#include <algorithm>
#include <cctype>
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

bool hasSafeScheme(const std::string &value)
{
    const auto delimiter = value.find_first_of("/?#");
    const auto colon = value.find(':');
    if (colon == std::string::npos ||
        (delimiter != std::string::npos && delimiter < colon))
        return true;

    auto scheme = value.substr(0, colon);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return scheme == "http" || scheme == "https" || scheme == "mailto";
}

void sanitizeUrlAttributes(std::string &html)
{
    for (const std::string marker : {" href=\"", " src=\""})
    {
        std::size_t offset = 0;
        while ((offset = html.find(marker, offset)) != std::string::npos)
        {
            const auto valueStart = offset + marker.size();
            const auto valueEnd = html.find('"', valueStart);
            if (valueEnd == std::string::npos)
                break;
            const auto value = html.substr(valueStart, valueEnd - valueStart);
            if (!hasSafeScheme(value))
            {
                const auto replacement = marker.starts_with(" href") ? "#" : "";
                html.replace(valueStart, value.size(), replacement);
                offset = valueStart + std::char_traits<char>::length(replacement);
            }
            else
            {
                offset = valueEnd + 1;
            }
        }
    }
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

    sanitizeUrlAttributes(output);

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
