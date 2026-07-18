#include "ContentRepository.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace blog
{
namespace
{
std::string stripCarriageReturn(std::string line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return line;
}

std::string requiredString(const Json::Value &value,
                           const char *name,
                           const std::filesystem::path &path)
{
    if (!value.isMember(name) || !value[name].isString() ||
        value[name].asString().empty())
    {
        throw std::runtime_error(path.string() + ": missing string field '" +
                                 name + "'");
    }
    return value[name].asString();
}

bool isSlug(const std::string &slug)
{
    if (slug.empty())
        return false;
    return std::all_of(slug.begin(), slug.end(), [](unsigned char ch) {
        return std::islower(ch) || std::isdigit(ch) || ch == '-';
    });
}

std::string today()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d");
    return output.str();
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return ch < 128 ? static_cast<char>(std::tolower(ch))
                        : static_cast<char>(ch);
    });
    return value;
}

bool contains(const std::string &text, const std::string &query)
{
    return lowerAscii(text).find(lowerAscii(query)) != std::string::npos;
}

std::size_t utf8Characters(const std::string &text)
{
    return static_cast<std::size_t>(std::count_if(
        text.begin(), text.end(), [](unsigned char ch) { return (ch & 0xc0) != 0x80; }));
}

Json::Value readPost(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open content file: " + path.string());

    std::string firstLine;
    std::getline(input, firstLine);
    if (firstLine.starts_with("\xef\xbb\xbf"))
        firstLine.erase(0, 3);
    if (stripCarriageReturn(firstLine) != "---")
        throw std::runtime_error(path.string() + ": expected JSON front matter");

    std::ostringstream metadataText;
    std::string line;
    bool closed = false;
    while (std::getline(input, line))
    {
        if (stripCarriageReturn(line) == "---")
        {
            closed = true;
            break;
        }
        metadataText << line << '\n';
    }
    if (!closed)
        throw std::runtime_error(path.string() + ": front matter is not closed");

    Json::Value metadata;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::string errors;
    std::istringstream metadataInput(metadataText.str());
    if (!Json::parseFromStream(builder, metadataInput, &metadata, &errors) ||
        !metadata.isObject())
    {
        throw std::runtime_error(path.string() + ": invalid JSON front matter: " +
                                 errors);
    }

    std::ostringstream markdown;
    markdown << input.rdbuf();
    const auto content = markdown.str();
    if (content.empty())
        throw std::runtime_error(path.string() + ": article body is empty");

    Json::Value post;
    post["slug"] = requiredString(metadata, "slug", path);
    post["title"] = requiredString(metadata, "title", path);
    post["excerpt"] = requiredString(metadata, "excerpt", path);
    post["publishedAt"] = requiredString(metadata, "date", path);
    post["updatedAt"] = metadata.get("updated", post["publishedAt"]);
    post["featured"] = metadata.get("featured", false).asBool();
    post["draft"] = metadata.get("draft", false).asBool();
    post["coverUrl"] = metadata.get("cover", "");
    post["seoTitle"] = metadata.get("seoTitle", "");
    post["seoDescription"] = metadata.get("seoDescription", "");
    post["content"] = content;
    post["sourcePath"] = path.filename().generic_string();
    post["readingMinutes"] = static_cast<Json::Int64>(
        (std::max)(std::size_t{1}, (utf8Characters(content) + 399) / 400));

    const auto slug = post["slug"].asString();
    if (!isSlug(slug))
        throw std::runtime_error(path.string() + ": invalid slug '" + slug + "'");
    if (path.stem().string() != slug)
        throw std::runtime_error(path.string() + ": filename must match slug");

    if (!metadata.isMember("category") || !metadata["category"].isObject())
        throw std::runtime_error(path.string() + ": category must be an object");
    post["categoryName"] =
        requiredString(metadata["category"], "name", path);
    post["categorySlug"] =
        requiredString(metadata["category"], "slug", path);
    if (!isSlug(post["categorySlug"].asString()))
        throw std::runtime_error(path.string() + ": invalid category slug");

    post["tags"] = Json::arrayValue;
    const auto &tags = metadata["tags"];
    if (!tags.isNull() && !tags.isArray())
        throw std::runtime_error(path.string() + ": tags must be an array");
    for (const auto &tag : tags)
    {
        if (!tag.isObject())
            throw std::runtime_error(path.string() + ": each tag must be an object");
        Json::Value normalizedTag;
        normalizedTag["name"] = requiredString(tag, "name", path);
        normalizedTag["slug"] = requiredString(tag, "slug", path);
        if (!isSlug(normalizedTag["slug"].asString()))
            throw std::runtime_error(path.string() + ": invalid tag slug");
        post["tags"].append(std::move(normalizedTag));
    }
    return post;
}

bool hasTag(const Json::Value &post, const std::string &slug)
{
    return std::any_of(post["tags"].begin(), post["tags"].end(),
                       [&slug](const Json::Value &tag) {
                           return tag["slug"].asString() == slug;
                       });
}
}  // namespace

ContentRepository::ContentRepository(std::filesystem::path contentDirectory)
{
    if (!std::filesystem::is_directory(contentDirectory))
        throw std::runtime_error("Content directory not found: " +
                                 contentDirectory.string());

    for (const auto &entry : std::filesystem::directory_iterator(contentDirectory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".md")
        {
            auto post = readPost(entry.path());
            if (!post["draft"].asBool() &&
                post["publishedAt"].asString() <= today())
                posts_.push_back(std::move(post));
        }
    }

    std::sort(posts_.begin(), posts_.end(), [](const auto &left, const auto &right) {
        if (left["featured"].asBool() != right["featured"].asBool())
            return left["featured"].asBool();
        return left["publishedAt"].asString() > right["publishedAt"].asString();
    });
}

Json::Value ContentRepository::listPublished(
    int page,
    int pageSize,
    const std::string &query,
    const std::string &categorySlug,
    const std::string &tagSlug) const
{
    page = (std::max)(1, page);
    pageSize = std::clamp(pageSize, 1, 24);
    std::vector<const Json::Value *> matches;
    for (const auto &post : posts_)
    {
        if (!query.empty() && !contains(post["title"].asString(), query) &&
            !contains(post["excerpt"].asString(), query) &&
            !contains(post["content"].asString(), query))
            continue;
        if (!categorySlug.empty() &&
            post["categorySlug"].asString() != categorySlug)
            continue;
        if (!tagSlug.empty() && !hasTag(post, tagSlug))
            continue;
        matches.push_back(&post);
    }

    Json::Value result;
    result["items"] = Json::arrayValue;
    const auto start = static_cast<std::size_t>((page - 1) * pageSize);
    const auto end = (std::min)(matches.size(), start + pageSize);
    for (auto index = start; index < end; ++index)
        result["items"].append(*matches[index]);
    result["page"] = page;
    result["pageSize"] = pageSize;
    result["total"] = static_cast<Json::Int64>(matches.size());
    result["pages"] = static_cast<int>((matches.size() + pageSize - 1) / pageSize);
    result["query"] = query;
    return result;
}

Json::Value ContentRepository::findPublishedBySlug(const std::string &slug) const
{
    const auto found = std::find_if(posts_.begin(), posts_.end(),
                                    [&slug](const auto &post) {
                                        return post["slug"].asString() == slug;
                                    });
    return found == posts_.end() ? Json::Value{} : *found;
}

Json::Value ContentRepository::listCategories() const
{
    std::map<std::string, Json::Value> categories;
    for (const auto &post : posts_)
    {
        auto &category = categories[post["categorySlug"].asString()];
        category["slug"] = post["categorySlug"];
        category["name"] = post["categoryName"];
        category["postCount"] = category.get("postCount", 0).asInt64() + 1;
    }
    Json::Value result(Json::arrayValue);
    for (const auto &[_, category] : categories)
        result.append(category);
    return result;
}

Json::Value ContentRepository::listTags() const
{
    std::map<std::string, Json::Value> tags;
    for (const auto &post : posts_)
    {
        for (const auto &postTag : post["tags"])
        {
            auto &tag = tags[postTag["slug"].asString()];
            tag["slug"] = postTag["slug"];
            tag["name"] = postTag["name"];
            tag["postCount"] = tag.get("postCount", 0).asInt64() + 1;
        }
    }
    Json::Value result(Json::arrayValue);
    for (const auto &[_, tag] : tags)
        result.append(tag);
    return result;
}

std::size_t ContentRepository::size() const noexcept
{
    return posts_.size();
}

std::filesystem::path configuredContentPath()
{
    const auto &custom = drogon::app().getCustomConfig();
    return custom.get("content_path", "./content/posts").asString();
}
}  // namespace blog
