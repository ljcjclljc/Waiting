#pragma once

#include <json/json.h>

#include <filesystem>
#include <string>
#include <vector>

namespace blog
{
class ContentRepository
{
  public:
    explicit ContentRepository(std::filesystem::path contentDirectory);

    Json::Value listPublished(int page,
                              int pageSize,
                              const std::string &query = {},
                              const std::string &categorySlug = {},
                              const std::string &tagSlug = {}) const;
    Json::Value findPublishedBySlug(const std::string &slug) const;
    Json::Value listAllPublished() const;
    Json::Value listArchive() const;
    Json::Value navigationForSlug(const std::string &slug) const;
    Json::Value relatedPublished(const std::string &slug,
                                 std::size_t limit = 3) const;
    Json::Value listCategories() const;
    Json::Value listTags() const;
    std::string categoryName(const std::string &slug) const;
    std::string tagName(const std::string &slug) const;
    std::size_t size() const noexcept;

  private:
    std::vector<Json::Value> posts_;
};

std::filesystem::path configuredContentPath();
}  // namespace blog
