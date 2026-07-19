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
#include <unordered_set>

namespace blog
{
namespace
{
constexpr std::uintmax_t kMaxPostBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxFrontMatterBytes = 64 * 1024;
constexpr std::size_t kMaxPosts = 1000;
constexpr Json::ArrayIndex kMaxTags = 32;

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
    if (std::filesystem::file_size(path) > kMaxPostBytes)
        throw std::runtime_error(path.string() + ": content file is too large");

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
        if (static_cast<std::size_t>(metadataText.tellp()) >
            kMaxFrontMatterBytes)
            throw std::runtime_error(path.string() +
                                     ": front matter is too large");
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
    if (tags.size() > kMaxTags)
        throw std::runtime_error(path.string() + ": too many tags");
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

std::vector<const Json::Value *> chronologicalPosts(
    const std::vector<Json::Value> &posts)
{
    std::vector<const Json::Value *> ordered;
    ordered.reserve(posts.size());
    for (const auto &post : posts)
        ordered.push_back(&post);
    std::sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right) {
        if ((*left)["publishedAt"].asString() !=
            (*right)["publishedAt"].asString())
            return (*left)["publishedAt"].asString() >
                   (*right)["publishedAt"].asString();
        return (*left)["slug"].asString() < (*right)["slug"].asString();
    });
    return ordered;
}

int relatedScore(const Json::Value &post, const Json::Value &candidate)
{
    int score = post["categorySlug"] == candidate["categorySlug"] ? 4 : 0;
    for (const auto &tag : post["tags"])
    {
        if (hasTag(candidate, tag["slug"].asString()))
            score += 2;
    }
    return score;
}
}  // namespace

ContentRepository::ContentRepository(std::filesystem::path contentDirectory)
{
    if (!std::filesystem::is_directory(contentDirectory))
        throw std::runtime_error("Content directory not found: " +
                                 contentDirectory.string());

    std::unordered_set<std::string> slugs;
    std::size_t markdownFiles = 0;
    for (const auto &entry : std::filesystem::directory_iterator(contentDirectory))
    {
        if (entry.path().extension() != ".md")
            continue;
        if (++markdownFiles > kMaxPosts)
            throw std::runtime_error("Content directory has too many Markdown files");
        if (entry.is_symlink())
            throw std::runtime_error("Content symlinks are not allowed: " +
                                     entry.path().filename().string());
        if (entry.is_regular_file() && entry.path().extension() == ".md")
        {
            auto post = readPost(entry.path());
            if (!slugs.insert(post["slug"].asString()).second)
                throw std::runtime_error("Duplicate content slug: " +
                                         post["slug"].asString());
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
    const auto pages = static_cast<int>((matches.size() + pageSize - 1) / pageSize);
    page = (std::min)(page, (std::max)(1, pages));
    const auto start = static_cast<std::size_t>(page - 1) *
                       static_cast<std::size_t>(pageSize);
    const auto end = (std::min)(matches.size(),
                                start + static_cast<std::size_t>(pageSize));
    for (auto index = start; index < end; ++index)
        result["items"].append(*matches[index]);
    result["page"] = page;
    result["pageSize"] = pageSize;
    result["total"] = static_cast<Json::Int64>(matches.size());
    result["pages"] = pages;
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

Json::Value ContentRepository::listAllPublished() const
{
    Json::Value result(Json::arrayValue);
    for (const auto *post : chronologicalPosts(posts_))
        result.append(*post);
    return result;
}

Json::Value ContentRepository::listArchive() const
{
    Json::Value result(Json::arrayValue);
    std::string currentYear;
    Json::Value group;
    for (const auto *post : chronologicalPosts(posts_))
    {
        const auto date = (*post)["publishedAt"].asString();
        const auto year = date.size() >= 4 ? date.substr(0, 4) : "其他";
        if (year != currentYear)
        {
            if (!currentYear.empty())
                result.append(std::move(group));
            currentYear = year;
            group = Json::Value(Json::objectValue);
            group["year"] = year;
            group["posts"] = Json::arrayValue;
        }
        group["posts"].append(*post);
    }
    if (!currentYear.empty())
        result.append(std::move(group));
    return result;
}

Json::Value ContentRepository::navigationForSlug(const std::string &slug) const
{
    Json::Value result(Json::objectValue);
    const auto ordered = chronologicalPosts(posts_);
    const auto found = std::find_if(ordered.begin(), ordered.end(), [&slug](const auto *post) {
        return (*post)["slug"].asString() == slug;
    });
    if (found == ordered.end())
        return result;

    const auto index = static_cast<std::size_t>(std::distance(ordered.begin(), found));
    if (index + 1 < ordered.size())
        result["previous"] = *ordered[index + 1];
    if (index > 0)
        result["next"] = *ordered[index - 1];
    return result;
}

Json::Value ContentRepository::relatedPublished(const std::string &slug,
                                                std::size_t limit) const
{
    Json::Value result(Json::arrayValue);
    const auto current = findPublishedBySlug(slug);
    if (current.isNull() || limit == 0)
        return result;

    struct Candidate
    {
        const Json::Value *post;
        int score;
    };
    std::vector<Candidate> candidates;
    for (const auto &post : posts_)
    {
        if (post["slug"].asString() != slug)
            candidates.push_back({&post, relatedScore(current, post)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        if (left.score != right.score)
            return left.score > right.score;
        return (*left.post)["publishedAt"].asString() >
               (*right.post)["publishedAt"].asString();
    });
    for (std::size_t index = 0; index < (std::min)(limit, candidates.size()); ++index)
        result.append(*candidates[index].post);
    return result;
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

std::string ContentRepository::categoryName(const std::string &slug) const
{
    const auto categories = listCategories();
    const auto found = std::find_if(categories.begin(), categories.end(), [&slug](const auto &item) {
        return item["slug"].asString() == slug;
    });
    return found == categories.end() ? std::string{} : (*found)["name"].asString();
}

std::string ContentRepository::tagName(const std::string &slug) const
{
    const auto tags = listTags();
    const auto found = std::find_if(tags.begin(), tags.end(), [&slug](const auto &item) {
        return item["slug"].asString() == slug;
    });
    return found == tags.end() ? std::string{} : (*found)["name"].asString();
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
