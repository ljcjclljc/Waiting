#include <drogon/drogon.h>

#include <filesystem>
#include <string>

int main(int argc, char *argv[])
{
    const std::string configPath =
        argc > 1 ? argv[1] : "config/config.dev.json";

    if (!std::filesystem::exists(configPath))
    {
        LOG_ERROR << "Configuration file not found: " << configPath;
        return 1;
    }

    drogon::app().loadConfigFile(configPath);
    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr &,
           const drogon::HttpResponsePtr &response) {
            response->addHeader("X-Content-Type-Options", "nosniff");
            response->addHeader("X-Frame-Options", "DENY");
            response->addHeader("Referrer-Policy", "strict-origin-when-cross-origin");
            response->addHeader(
                "Permissions-Policy",
                "camera=(), microphone=(), geolocation=()");
            response->addHeader(
                "Content-Security-Policy",
                "default-src 'self'; img-src 'self' data: https:; "
                "style-src 'self' https://giscus.app; "
                "script-src 'self' https://giscus.app; "
                "connect-src 'self' https://giscus.app https://api.github.com; "
                "frame-src https://giscus.app; base-uri 'self'; "
                "form-action 'self'; frame-ancestors 'none'");
        });

    LOG_INFO << "Starting Drogon Blog with " << configPath;
    drogon::app().run();
    return 0;
}
