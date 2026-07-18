#pragma once

#include "Html.h"

#include <json/json.h>

#include <sstream>
#include <string>

namespace blog::view
{
inline std::string pageHeader(const Json::Value &site,
                              const std::string &title,
                              const std::string &description)
{
    const auto siteName = site.get("name", "Blog").asString();
    const auto repositoryUrl = site["github"].get("repository_url", "").asString();
    std::ostringstream output;
    output << "<!doctype html><html lang=\"zh-CN\"><head>"
           << "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           << "<title>" << html::escape(title.empty() ? siteName : title + " | " + siteName)
           << "</title><meta name=\"description\" content=\""
           << html::escape(description) << "\">"
           << "<link rel=\"alternate\" type=\"application/rss+xml\" title=\"RSS\" href=\"/feed.xml\">"
           << "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">"
           << "<link rel=\"stylesheet\" href=\"/css/app.css\">"
           << "<script src=\"/js/app.js\"></script></head>"
           << "<body data-surface=\"site\">"
           << "<a class=\"skip-link\" href=\"#main\">跳到正文</a>"
           << "<header class=\"site-header\"><div class=\"shell header-inner\">"
           << "<a class=\"brand\" href=\"/\" aria-label=\"返回首页\"><span class=\"brand-mark\">V</span><span>"
           << html::escape(siteName) << "</span></a>"
           << "<button class=\"nav-toggle icon-button\" type=\"button\" aria-label=\"打开导航\" aria-expanded=\"false\">"
           << "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 7h16M4 12h16M4 17h16\"/></svg></button>"
           << "<nav class=\"main-nav\" aria-label=\"主导航\">"
           << "<a href=\"/posts\">文章</a><a href=\"/#topics\">主题</a>"
           << "<a href=\"/search\">搜索</a><a href=\"/feed.xml\">RSS</a>";
    if (!repositoryUrl.empty())
        output << "<a href=\"" << html::escape(repositoryUrl)
               << "\" target=\"_blank\" rel=\"noopener noreferrer\">GitHub</a>";
    output << "</nav><button class=\"theme-toggle icon-button\" type=\"button\" aria-label=\"切换深色模式\">"
           << "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3v2m0 14v2M3 12h2m14 0h2M5.64 5.64l1.42 1.42m9.88 9.88 1.42 1.42m0-12.72-1.42 1.42M7.06 16.94l-1.42 1.42\"/><circle cx=\"12\" cy=\"12\" r=\"4\"/></svg>"
           << "</button></div></header>";
    return output.str();
}

inline std::string pageFooter(const Json::Value &site)
{
    const auto repositoryUrl = site["github"].get("repository_url", "").asString();
    std::ostringstream output;
    output << "<footer class=\"site-footer\"><div class=\"shell footer-inner\"><div>"
           << "<strong>" << html::escape(site.get("name", "Blog").asString())
           << "</strong><p>" << html::escape(site.get("description", "").asString())
           << "</p></div><div class=\"footer-links\"><a href=\"/feed.xml\">RSS</a>"
           << "<a href=\"/sitemap.xml\">站点地图</a>";
    if (!repositoryUrl.empty())
        output << "<a href=\"" << html::escape(repositoryUrl)
               << "\" target=\"_blank\" rel=\"noopener noreferrer\">源码</a>";
    output << "<span>Drogon · Markdown · GitHub</span></div>"
           << "</div></footer></body></html>";
    return output.str();
}

inline std::string postCard(const Json::Value &post, bool compact = false)
{
    std::ostringstream output;
    output << "<article class=\"post-card" << (compact ? " post-card--compact" : "")
           << "\"><div class=\"post-meta\"><a href=\"/categories/"
           << html::attr(post["categorySlug"]) << "\">"
           << html::text(post["categoryName"]) << "</a><span>"
           << html::text(post["publishedAt"]) << "</span><span>"
           << post.get("readingMinutes", 1).asInt64() << " 分钟阅读</span></div>"
           << "<h2><a href=\"/posts/" << html::attr(post["slug"]) << "\">"
           << html::text(post["title"]) << "</a></h2><p>"
           << html::text(post["excerpt"]) << "</p><a class=\"text-link\" href=\"/posts/"
           << html::attr(post["slug"]) << "\">阅读文章<span aria-hidden=\"true\"> →</span></a></article>";
    return output.str();
}

inline std::string categoryLink(const Json::Value &category)
{
    std::ostringstream output;
    output << "<a class=\"topic-row\" href=\"/categories/"
           << html::attr(category["slug"]) << "\"><span>"
           << html::text(category["name"]) << "</span><small>"
           << category.get("postCount", 0).asInt64() << " 篇</small></a>";
    return output.str();
}

inline std::string tagLink(const Json::Value &tag)
{
    return "<a href=\"/tags/" + html::attr(tag["slug"]) + "\">#" +
           html::text(tag["name"]) + "</a>";
}

inline std::string sourceUrl(const Json::Value &site, const Json::Value &post)
{
    const auto base = site["github"].get("repository_url", "").asString();
    const auto branch = site["github"].get("branch", "main").asString();
    if (base.empty())
        return {};
    return base + "/blob/" + branch + "/content/posts/" +
           html::urlEncode(post["sourcePath"].asString());
}
}  // namespace blog::view
