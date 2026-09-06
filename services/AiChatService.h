#pragma once

#include <drogon/drogon.h>

#include <filesystem>
#include <string>

namespace blog
{
struct ChatResult
{
    bool ok{false};
    int status{500};
    std::string answer;
    std::string message;
    std::size_t sourceCount{0};
};

class AiChatService
{
  public:
    static AiChatService &instance();

    drogon::Task<ChatResult> ask(const Json::Value &payload,
                                 const std::string &clientAddress);

  private:
    struct Config
    {
        std::string apiKey;
        std::string apiUrl{"https://open.bigmodel.cn"};
        std::string apiPath{"/api/paas/v4/chat/completions"};
        std::string model{"glm-4.5-air"};
        std::filesystem::path knowledgePath{"./knowledge_base"};
        std::filesystem::path promptPath{"./prompt_optimization"};
    };

    AiChatService() = default;
    Config config() const;
    static std::string loadDirectory(const std::filesystem::path &directory,
                                     std::size_t maxBytes,
                                     std::size_t &fileCount);
};
}  // namespace blog
