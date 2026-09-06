#include <drogon/drogon.h>

#include "repositories/ContentRepository.h"
#include "repositories/ContentStore.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string_view(argv[1]) == "--validate-content")
    {
        if (argc != 3)
        {
            std::cerr << "Usage: drogon_blog --validate-content <posts-directory>\n";
            return 2;
        }
        try
        {
            const blog::ContentRepository repository(argv[2]);
            std::cout << "Content valid: " << repository.size()
                      << " published posts\n";
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Content validation failed: " << error.what() << '\n';
            return 1;
        }
    }

    const std::string configPath = argc > 1
                                       ? argv[1]
                                       : (std::filesystem::exists(
                                              "config/config.local.json")
                                              ? "config/config.local.json"
                                              : "config/config.dev.json");

    if (!std::filesystem::exists(configPath))
    {
        LOG_ERROR << "Configuration file not found: " << configPath;
        return 1;
    }

    drogon::app().loadConfigFile(configPath);
    try
    {
        (void)blog::contentStore();
    }
    catch (const std::exception &error)
    {
        LOG_ERROR << "Initial content load failed: " << error.what();
        return 1;
    }
    drogon::app().getLoop()->runEvery(2.0, [] {
        blog::contentStore().reloadIfChanged();
    });
    {
        drogon::HttpViewData data;
        data.insert("message", std::string("你访问的页面不存在，可能已被移动或删除。"));
        auto response = drogon::HttpResponse::newHttpViewResponse("Error", data);
        drogon::app().setCustom404Page(response);
    }
    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr &,
           const drogon::HttpResponsePtr &response) {
            response->addHeader("X-Content-Type-Options", "nosniff");
            response->addHeader("X-Frame-Options", "DENY");
            response->addHeader("Cross-Origin-Resource-Policy", "same-origin");
            response->addHeader("Referrer-Policy", "strict-origin-when-cross-origin");
            response->addHeader(
                "Permissions-Policy",
                "camera=(), microphone=(), geolocation=()");
            response->addHeader(
                "Content-Security-Policy",
                "default-src 'self'; img-src 'self' data: https:; "
                "style-src 'self' https://giscus.app; "
                "script-src 'self' https://giscus.app; "
                "media-src 'self'; "
                "connect-src 'self' https://giscus.app https://api.github.com; "
                "frame-src https://giscus.app; base-uri 'self'; "
                "form-action 'self'; frame-ancestors 'none'");
        });

    LOG_INFO << "Starting Drogon Blog with " << configPath;
    drogon::app().run();
    return 0;
}
