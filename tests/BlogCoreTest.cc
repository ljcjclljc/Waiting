#define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>

#include "repositories/ContentRepository.h"
#include "services/MarkdownService.h"

#include <chrono>
#include <filesystem>
#include <fstream>

DROGON_TEST(MarkdownSafety)
{
    const auto html = blog::MarkdownService::render(
        "# Heading\n\n<script>alert('x')</script>\n\n[link](https://example.com)");
    CHECK(html.find("<h1>") != std::string::npos);
    CHECK(html.find("<script>") == std::string::npos);
    CHECK(html.find("noopener") != std::string::npos);
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

    std::filesystem::remove_all(directory);
}

int main(int argc, char **argv)
{
    return drogon::test::run(argc, argv);
}
