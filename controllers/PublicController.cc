#include "PublicController.h"

#include "Response.h"
#include "repositories/ContentRepository.h"
#include "services/Html.h"
#include "services/MarkdownService.h"
#include "services/SiteConfig.h"

#include <algorithm>
#include <sstream>

namespace blog
{
namespace
{
int pageParameter(const drogon::HttpRequestPtr &request)
{
    try
    {
        return (std::max)(1, std::stoi(request->getParameter("page")));
    }
    catch (...)
    {
        return 1;
    }
}

drogon::HttpResponsePtr view(const std::string &name, drogon::HttpViewData data)
{
    data.insert("site", siteConfig());
    auto response = drogon::HttpResponse::newHttpViewResponse(name, data);
    response->addHeader("Cache-Control", "public, max-age=30");
    return response;
}

ContentRepository content()
{
    return ContentRepository(configuredContentPath());
}
}  // namespace

drogon::Task<drogon::HttpResponsePtr> PublicController::home(
    drogon::HttpRequestPtr)
{
    try
    {
        const auto repository = content();
        drogon::HttpViewData data;
        data.insert("posts", repository.listPublished(1, 7));
        data.insert("categories", repository.listCategories());
        data.insert("tags", repository.listTags());
        co_return view("Home", std::move(data));
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << error.what();
        co_return viewError("文章内容暂时不可用，请检查 Markdown 文件。",
                            drogon::k503ServiceUnavailable);
    }
}

drogon::Task<drogon::HttpResponsePtr> PublicController::posts(
    drogon::HttpRequestPtr request)
{
    try
    {
        const auto repository = content();
        drogon::HttpViewData data;
        data.insert("title", std::string("全部文章"));
        data.insert("posts", repository.listPublished(pageParameter(request), 9));
        data.insert("categories", repository.listCategories());
        co_return view("PostList", std::move(data));
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << error.what();
        co_return viewError("无法读取文章列表。",
                            drogon::k503ServiceUnavailable);
    }
}

drogon::Task<drogon::HttpResponsePtr> PublicController::article(
    drogon::HttpRequestPtr,
    std::string slug)
{
    try
    {
        const auto repository = content();
        auto post = repository.findPublishedBySlug(slug);
        if (post.isNull() || post.empty())
            co_return viewError("这篇文章不存在或尚未发布。",
                                drogon::k404NotFound);
        post["contentHtml"] = MarkdownService::render(post["content"].asString());
        drogon::HttpViewData data;
        data.insert("post", post);
        auto response = view("PostDetail", std::move(data));
        response->addHeader("Cache-Control", "public, max-age=60");
        co_return response;
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << error.what();
        co_return viewError("无法读取文章。", drogon::k503ServiceUnavailable);
    }
}

drogon::Task<drogon::HttpResponsePtr> PublicController::category(
    drogon::HttpRequestPtr request,
    std::string slug)
{
    try
    {
        const auto repository = content();
        drogon::HttpViewData data;
        data.insert("title", std::string("分类：") + slug);
        data.insert("posts", repository.listPublished(
                                 pageParameter(request), 9, {}, slug, {}));
        data.insert("categories", repository.listCategories());
        co_return view("PostList", std::move(data));
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << error.what();
        co_return viewError("无法读取分类。", drogon::k503ServiceUnavailable);
    }
}

drogon::Task<drogon::HttpResponsePtr> PublicController::tag(
    drogon::HttpRequestPtr request,
    std::string slug)
{
    try
    {
        const auto repository = content();
        drogon::HttpViewData data;
        data.insert("title", std::string("标签：") + slug);
        data.insert("posts", repository.listPublished(
                                 pageParameter(request), 9, {}, {}, slug));
        data.insert("categories", repository.listCategories());
        co_return view("PostList", std::move(data));
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << error.what();
        co_return viewError("无法读取标签。", drogon::k503ServiceUnavailable);
    }
}

drogon::Task<drogon::HttpResponsePtr> PublicController::search(
    drogon::HttpRequestPtr request)
{
    auto query = request->getParameter("q");
    if (query.size() > 80)
        query.resize(80);
    try
    {
        const auto repository = content();
        drogon::HttpViewData data;
        data.insert("title", std::string("搜索"));
        data.insert("query", query);
        data.insert("posts", repository.listPublished(
                                 pageParameter(request), 9, query));
        data.insert("categories", repository.listCategories());
        co_return view("PostList", std::move(data));
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << error.what();
        co_return viewError("搜索暂时不可用。",
                            drogon::k503ServiceUnavailable);
    }
}

drogon::Task<drogon::HttpResponsePtr> PublicController::feed(
    drogon::HttpRequestPtr)
{
    const auto posts = content().listPublished(1, 20);
    const auto site = siteConfig();
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<rss version=\"2.0\"><channel><title>"
        << html::escape(site.get("name", "Blog").asString())
        << "</title><link>" << html::escape(site.get("url", "").asString())
        << "</link><description>"
        << html::escape(site.get("description", "").asString()) << "</description>";
    for (const auto &post : posts["items"])
    {
        xml << "<item><title>" << html::text(post["title"])
            << "</title><link>" << html::escape(site["url"].asString())
            << "/posts/" << html::attr(post["slug"])
            << "</link><description>" << html::text(post["excerpt"])
            << "</description></item>";
    }
    xml << "</channel></rss>";
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCode(drogon::CT_APPLICATION_XML);
    response->setBody(xml.str());
    co_return response;
}

drogon::Task<drogon::HttpResponsePtr> PublicController::sitemap(
    drogon::HttpRequestPtr)
{
    const auto posts = content().listPublished(1, 24);
    const auto base = siteConfig().get("url", "").asString();
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">"
        << "<url><loc>" << html::escape(base) << "/</loc></url>";
    for (const auto &post : posts["items"])
        xml << "<url><loc>" << html::escape(base) << "/posts/"
            << html::attr(post["slug"]) << "</loc><lastmod>"
            << html::text(post["updatedAt"]) << "</lastmod></url>";
    xml << "</urlset>";
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCode(drogon::CT_APPLICATION_XML);
    response->setBody(xml.str());
    co_return response;
}

drogon::Task<drogon::HttpResponsePtr> PublicController::robots(
    drogon::HttpRequestPtr)
{
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    response->setBody("User-agent: *\nAllow: /\nSitemap: " +
                      siteConfig().get("url", "").asString() +
                      "/sitemap.xml\n");
    co_return response;
}

drogon::Task<drogon::HttpResponsePtr> PublicController::health(
    drogon::HttpRequestPtr)
{
    Json::Value body;
    try
    {
        const auto repository = content();
        body["ok"] = true;
        body["content"] = "up";
        body["publishedPosts"] = static_cast<Json::Int64>(repository.size());
        co_return jsonResponse(std::move(body));
    }
    catch (const std::exception &error)
    {
        body["ok"] = false;
        body["content"] = "down";
        body["error"] = error.what();
        co_return jsonResponse(std::move(body), drogon::k503ServiceUnavailable);
    }
}
}  // namespace blog
