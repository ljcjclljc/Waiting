#pragma once

#include "Html.h"

#include <json/json.h>

#include <sstream>
#include <string>

namespace blog::view
{
struct PageMeta
{
    std::string title;
    std::string description;
    std::string path;
    std::string image;
    std::string publishedAt;
    std::string updatedAt;
    bool article{false};
    bool home{false};
    bool index{true};
};

inline std::string absoluteUrl(const std::string &base, const std::string &value)
{
    if (value.starts_with("http://") || value.starts_with("https://"))
        return value;
    auto normalizedBase = base;
    while (!normalizedBase.empty() && normalizedBase.back() == '/')
        normalizedBase.pop_back();
    if (value.empty() || value == "/")
        return normalizedBase + "/";
    return normalizedBase + (value.front() == '/' ? "" : "/") + value;
}

inline std::string pageHeader(const Json::Value &site,
                              const PageMeta &meta)
{
    const auto siteName = site.get("name", "Blog").asString();
    const auto title = meta.title.empty() ? siteName : meta.title + " | " + siteName;
    const auto canonical = absoluteUrl(site.get("url", "").asString(), meta.path);
    const auto image = absoluteUrl(
        site.get("url", "").asString(),
        meta.image.empty() ? "/images/kayoko-rain.jpg" : meta.image);
    const auto repositoryUrl = site["github"].get("repository_url", "").asString();
    std::ostringstream output;
    output << "<!doctype html><html lang=\"zh-CN\"><head>"
           << "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           << "<title>" << html::escape(title)
           << "</title><meta name=\"description\" content=\""
           << html::attr(meta.description) << "\"><meta name=\"author\" content=\""
           << html::attr(site.get("author", "").asString()) << "\">"
           << "<link rel=\"canonical\" href=\"" << html::attr(canonical) << "\">"
           << "<meta property=\"og:locale\" content=\"zh_CN\">"
           << "<meta property=\"og:type\" content=\"" << (meta.article ? "article" : "website") << "\">"
           << "<meta property=\"og:site_name\" content=\"" << html::attr(siteName) << "\">"
           << "<meta property=\"og:title\" content=\"" << html::attr(title) << "\">"
           << "<meta property=\"og:description\" content=\"" << html::attr(meta.description) << "\">"
           << "<meta property=\"og:url\" content=\"" << html::attr(canonical) << "\">"
           << "<meta property=\"og:image\" content=\"" << html::attr(image) << "\">"
           << "<meta name=\"twitter:card\" content=\"summary_large_image\">"
           << "<meta name=\"twitter:title\" content=\"" << html::attr(title) << "\">"
           << "<meta name=\"twitter:description\" content=\"" << html::attr(meta.description) << "\">"
           << "<meta name=\"twitter:image\" content=\"" << html::attr(image) << "\">";
    if (!meta.index)
        output << "<meta name=\"robots\" content=\"noindex,follow\">";
    if (meta.article)
    {
        output << "<meta property=\"article:published_time\" content=\""
               << html::attr(meta.publishedAt) << "\">"
               << "<meta property=\"article:modified_time\" content=\""
               << html::attr(meta.updatedAt) << "\">";
    }
    output
           << "<link rel=\"alternate\" type=\"application/rss+xml\" title=\"RSS\" href=\"/feed.xml\">"
           << "<link rel=\"icon\" href=\"/images/avatar-rossi.jpg\" type=\"image/jpeg\">"
           << "<link rel=\"stylesheet\" href=\"/css/app.css?v=20260720-1\">"
           << "<script src=\"/js/app.js?v=20260720-1\" defer></script></head>"
           << "<body data-surface=\"site\" data-page=\"" << (meta.home ? "home" : "inner") << "\">";
    if (meta.home)
        output << "<div class=\"site-background\" aria-hidden=\"true\"><video autoplay muted loop playsinline preload=\"auto\" poster=\"/images/kayoko-rain.jpg\" tabindex=\"-1\"><source src=\"/media/kayoko-rain.mp4\" type=\"video/mp4\"></video><div class=\"site-background-shade\"></div></div>";
    output << "<a class=\"skip-link\" href=\"#main\">跳到正文</a>"
           << "<header class=\"site-header\"><div class=\"shell header-inner\">"
           << "<a class=\"brand\" href=\"/\" aria-label=\"返回首页\"><img class=\"brand-avatar\" src=\"/images/avatar-rossi.jpg\" alt=\"\" width=\"36\" height=\"36\" decoding=\"async\">"
           << "<span class=\"brand-copy\"><b>" << html::escape(siteName) << "</b><small>CHEN'S BLOG</small></span></a>"
           << "<button class=\"nav-toggle icon-button\" type=\"button\" aria-label=\"打开导航\" aria-expanded=\"false\">"
           << "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M4 7h16M4 12h16M4 17h16\"/></svg></button>"
           << "<nav class=\"main-nav\" aria-label=\"主导航\">"
           << "<a href=\"/#topics\">主题</a><a href=\"/posts\">文章</a><a href=\"/archives\">归档</a>"
           << "<a href=\"/search\">搜索</a><a href=\"/feed.xml\">RSS</a>";
    if (!repositoryUrl.empty())
        output << "<a href=\"" << html::escape(repositoryUrl)
               << "\" target=\"_blank\" rel=\"noopener noreferrer\">GitHub</a>";
    output << "</nav><button class=\"theme-toggle icon-button\" type=\"button\" aria-label=\"切换深色模式\" title=\"切换主题\">"
           << "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M12 3v2m0 14v2M3 12h2m14 0h2M5.64 5.64l1.42 1.42m9.88 9.88 1.42 1.42m0-12.72-1.42 1.42M7.06 16.94l-1.42 1.42\"/><circle cx=\"12\" cy=\"12\" r=\"4\"/></svg>"
           << "</button></div></header>";
    return output.str();
}

inline std::string pageHeader(const Json::Value &site,
                              const std::string &title,
                              const std::string &description)
{
    PageMeta meta;
    meta.title = title;
    meta.description = description;
    meta.path = title.empty() ? "/" : "";
    meta.home = title.empty();
    return pageHeader(site, meta);
}

inline std::string pageFooter(const Json::Value &site)
{
    const auto repositoryUrl = site["github"].get("repository_url", "").asString();
    std::ostringstream output;
    output << "<footer class=\"site-footer\"><div class=\"shell footer-inner\"><div class=\"footer-brand\"><img class=\"footer-avatar\" src=\"/images/avatar-rossi.jpg\" alt=\"\" width=\"48\" height=\"48\" loading=\"lazy\" decoding=\"async\"><div>"
           << "<strong>" << html::escape(site.get("name", "Blog").asString())
           << "</strong><p>" << html::escape(site.get("description", "").asString())
           << "</p></div></div><div class=\"footer-links\"><a href=\"/archives\">归档</a><a href=\"/feed.xml\">RSS</a>"
           << "<a href=\"/sitemap.xml\">站点地图</a>";
    if (!repositoryUrl.empty())
        output << "<a href=\"" << html::escape(repositoryUrl)
               << "\" target=\"_blank\" rel=\"noopener noreferrer\">源码</a>";
    output << "<span>Drogon · Markdown · GitHub</span></div>"
           << "</div><div class=\"shell footer-base\"><span>BUILD FROM SOURCE</span><span>© CHEN'S BLOG</span></div></footer></body></html>";
    return output.str();
}

inline std::string postCard(const Json::Value &post, bool compact = false)
{
    std::ostringstream output;
    output << "<article class=\"post-card" << (compact ? " post-card--compact" : "")
           << "\" data-reveal><div class=\"post-meta\"><a href=\"/categories/"
           << html::attr(post["categorySlug"]) << "\">"
           << html::text(post["categoryName"]) << "</a><span>"
           << html::text(post["publishedAt"]) << "</span><span>"
           << post.get("readingMinutes", 1).asInt64() << " 分钟阅读</span></div>"
           << "<h2><a href=\"/posts/" << html::attr(post["slug"]) << "\">"
           << html::text(post["title"]) << "</a></h2><p>"
           << html::text(post["excerpt"]) << "</p><a class=\"card-arrow\" aria-label=\"阅读文章："
           << html::attr(post["title"]) << "\" href=\"/posts/"
           << html::attr(post["slug"]) << "\"><svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><path d=\"M5 12h14m-5-5 5 5-5 5\"/></svg></a></article>";
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
