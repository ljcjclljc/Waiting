#pragma once

#include <drogon/HttpController.h>

namespace blog
{
class PublicController : public drogon::HttpController<PublicController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PublicController::home, "/", drogon::Get);
    ADD_METHOD_TO(PublicController::posts, "/posts", drogon::Get);
    ADD_METHOD_TO(PublicController::archives, "/archives", drogon::Get);
    ADD_METHOD_TO(PublicController::article, "/posts/{slug}", drogon::Get);
    ADD_METHOD_TO(PublicController::category,
                  "/categories/{slug}",
                  drogon::Get);
    ADD_METHOD_TO(PublicController::tag, "/tags/{slug}", drogon::Get);
    ADD_METHOD_TO(PublicController::search, "/search", drogon::Get);
    ADD_METHOD_TO(PublicController::feed, "/feed.xml", drogon::Get);
    ADD_METHOD_TO(PublicController::sitemap, "/sitemap.xml", drogon::Get);
    ADD_METHOD_TO(PublicController::robots, "/robots.txt", drogon::Get);
    ADD_METHOD_TO(PublicController::health, "/health", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> home(drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> posts(drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> archives(
        drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> article(
        drogon::HttpRequestPtr request,
        std::string slug);
    drogon::Task<drogon::HttpResponsePtr> category(
        drogon::HttpRequestPtr request,
        std::string slug);
    drogon::Task<drogon::HttpResponsePtr> tag(drogon::HttpRequestPtr request,
                                              std::string slug);
    drogon::Task<drogon::HttpResponsePtr> search(drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> feed(drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> sitemap(
        drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> robots(
        drogon::HttpRequestPtr request);
    drogon::Task<drogon::HttpResponsePtr> health(drogon::HttpRequestPtr request);
};
}  // namespace blog
