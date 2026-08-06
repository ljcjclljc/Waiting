#pragma once

#include "ContentRepository.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace blog
{
struct ContentReloadStatus
{
    std::string activeVersion;
    std::string observedVersion;
    std::string lastError;
    std::size_t publishedPosts{0};
};

class ContentStore
{
  public:
    explicit ContentStore(std::filesystem::path contentDirectory);

    std::shared_ptr<const ContentRepository> snapshot() const noexcept;
    bool reloadIfChanged();
    ContentReloadStatus status() const;

  private:
    std::filesystem::path contentDirectory_;
    std::filesystem::path versionFile_;
    std::shared_ptr<const ContentRepository> current_;
    mutable std::mutex reloadMutex_;
    std::string activeVersion_{"startup"};
    std::string observedVersion_;
    std::string activeContentFingerprint_;
    std::string lastError_;
};

ContentStore &contentStore();
}  // namespace blog
