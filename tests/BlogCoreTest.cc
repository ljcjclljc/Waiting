#define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>

#include "repositories/ContentRepository.h"
#include "services/MarkdownService.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

DROGON_TEST(MarkdownSafety)
{
    const auto html = blog::MarkdownService::render(
        "# Heading\n\n<script>alert('x')</script>\n\n[link](https://example.com)");
    CHECK(html.find("<h1>") != std::string::npos);
    CHECK(html.find("<script>") == std::string::npos);
    CHECK(html.find("noopener") != std::string::npos);

    const auto unsafe = blog::MarkdownService::render(
        "[link](javascript:alert(document.domain))\n\n"
        "![image](data:text/html,unsafe)");
    CHECK(unsafe.find("javascript:") == std::string::npos);
    CHECK(unsafe.find("data:text/html") == std::string::npos);
}

DROGON_TEST(ContentRepositoryReadsPublishedMarkdown)
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("drogon-blog-content-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    const auto path = directory / "hello-drogon.md";
    {
        std::ofstream output(path, std::ios::binary);
        output << R"(---
{
  "title": "Hello Drogon",
  "slug": "hello-drogon",
  "date": "2026-01-01",
  "excerpt": "A test article",
  "category": { "name": "C++", "slug": "cpp" },
  "tags": [{ "name": "Drogon", "slug": "drogon" }],
  "featured": true,
  "draft": false
}
---
# Content
This is searchable.
)";
    }

    const blog::ContentRepository repository(directory);
    CHECK(repository.size() == 1);
    CHECK(repository.findPublishedBySlug("hello-drogon")["title"].asString() ==
          "Hello Drogon");
    CHECK(repository.listPublished(1, 10, "searchable")["total"].asInt() == 1);
    CHECK(repository.listCategories()[0]["postCount"].asInt() == 1);
    CHECK(repository.listTags()[0]["slug"].asString() == "drogon");
    const auto lastPage = repository.listPublished(
        (std::numeric_limits<int>::max)(), 10);
    CHECK(lastPage["page"].asInt() == 1);
    CHECK(lastPage["items"].size() == 1);

    std::filesystem::remove_all(directory);
}

DROGON_TEST(ContentRepositoryRejectsOversizedContent)
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("drogon-blog-large-content-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    {
        std::ofstream output(directory / "too-large.md", std::ios::binary);
        output << std::string(2 * 1024 * 1024 + 1, 'x');
    }

    bool rejected = false;
    try
    {
        const blog::ContentRepository repository(directory);
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    CHECK(rejected);
    std::filesystem::remove_all(directory);
}

DROGON_TEST(ContentRepositoryBuildsArchiveAndReadingLinks)
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("drogon-blog-navigation-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);

    const auto writePost = [&directory](const std::string &slug,
                                        const std::string &title,
                                        const std::string &date,
                                        const std::string &category,
                                        const std::string &categorySlug,
                                        const std::string &tag,
                                        const std::string &tagSlug) {
        std::ofstream output(directory / (slug + ".md"), std::ios::binary);
        output << "---\n{\n"
               << "  \"title\": \"" << title << "\",\n"
               << "  \"slug\": \"" << slug << "\",\n"
               << "  \"date\": \"" << date << "\",\n"
               << "  \"excerpt\": \"Test excerpt\",\n"
               << "  \"category\": { \"name\": \"" << category
               << "\", \"slug\": \"" << categorySlug << "\" },\n"
               << "  \"tags\": [{ \"name\": \"" << tag
               << "\", \"slug\": \"" << tagSlug << "\" }],\n"
               << "  \"draft\": false\n}\n---\n# Content\n";
    };

    writePost("newest", "Newest", "2026-03-03", "C++", "cpp", "Drogon", "drogon");
    writePost("middle", "Middle", "2026-02-02", "C++", "cpp", "Drogon", "drogon");
    writePost("oldest", "Oldest", "2025-01-01", "Database", "database", "SQL", "sql");

    const blog::ContentRepository repository(directory);
    const auto all = repository.listAllPublished();
    CHECK(all.size() == 3);
    CHECK(all[0]["slug"].asString() == "newest");

    const auto archive = repository.listArchive();
    CHECK(archive.size() == 2);
    CHECK(archive[0]["year"].asString() == "2026");
    CHECK(archive[0]["posts"].size() == 2);

    const auto navigation = repository.navigationForSlug("middle");
    CHECK(navigation["previous"]["slug"].asString() == "oldest");
    CHECK(navigation["next"]["slug"].asString() == "newest");

    const auto related = repository.relatedPublished("newest", 1);
    CHECK(related.size() == 1);
    CHECK(related[0]["slug"].asString() == "middle");
    CHECK(repository.categoryName("cpp") == "C++");
    CHECK(repository.tagName("drogon") == "Drogon");

    std::filesystem::remove_all(directory);
}

int main(int argc, char **argv)
{
    return drogon::test::run(argc, argv);
}
