#include "ContentStore.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace blog
{
namespace
{
constexpr std::size_t kMaxVersionBytes = 128;

std::string readVersionFile(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path))
        return {};
    if (std::filesystem::is_symlink(path) ||
        !std::filesystem::is_regular_file(path))
        throw std::runtime_error("Content version marker must be a regular file");
    if (std::filesystem::file_size(path) > kMaxVersionBytes)
        throw std::runtime_error("Content version marker is too large");

    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read content version marker");
    std::string value{std::istreambuf_iterator<char>(input),
                      std::istreambuf_iterator<char>()};
    const auto whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(value.begin(),
                std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
                value.end());
    return value;
}

bool isCommitSha(const std::string &value)
{
    return value.size() == 40 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}
}  // namespace

ContentStore::ContentStore(std::filesystem::path contentDirectory)
    : contentDirectory_(std::move(contentDirectory)),
      versionFile_(contentDirectory_.parent_path() / ".content-version")
{
    auto initial =
        std::make_shared<const ContentRepository>(contentDirectory_);
    std::atomic_store_explicit(&current_, std::move(initial),
                               std::memory_order_release);
    try
    {
        observedVersion_ = readVersionFile(versionFile_);
        if (isCommitSha(observedVersion_))
            activeVersion_ = observedVersion_;
        else if (!observedVersion_.empty())
            lastError_ = "Content version marker is not a 40-character commit SHA";
    }
    catch (const std::exception &error)
    {
        lastError_ = error.what();
    }
}

std::shared_ptr<const ContentRepository> ContentStore::snapshot() const noexcept
{
    return std::atomic_load_explicit(&current_, std::memory_order_acquire);
}

bool ContentStore::reloadIfChanged()
{
    std::string candidateVersion;
    try
    {
        candidateVersion = readVersionFile(versionFile_);
    }
    catch (const std::exception &error)
    {
        std::lock_guard lock(reloadMutex_);
        lastError_ = error.what();
        LOG_ERROR << "Content reload marker rejected: " << lastError_;
        return false;
    }

    if (candidateVersion.empty())
        return false;

    std::lock_guard lock(reloadMutex_);
    if (candidateVersion == observedVersion_)
        return false;
    observedVersion_ = candidateVersion;

    if (!isCommitSha(candidateVersion))
    {
        lastError_ = "Content version marker is not a 40-character commit SHA";
        LOG_ERROR << "Content reload marker rejected: " << lastError_;
        return false;
    }

    try
    {
        auto next =
            std::make_shared<const ContentRepository>(contentDirectory_);
        std::atomic_store_explicit(&current_, std::move(next),
                                   std::memory_order_release);
        activeVersion_ = candidateVersion;
        lastError_.clear();
        LOG_INFO << "Content reloaded at commit " << activeVersion_;
        return true;
    }
    catch (const std::exception &error)
    {
        lastError_ = error.what();
        LOG_ERROR << "Content reload failed for commit " << candidateVersion
                  << ": " << lastError_;
        return false;
    }
}

ContentReloadStatus ContentStore::status() const
{
    std::lock_guard lock(reloadMutex_);
    return {activeVersion_,
            observedVersion_,
            lastError_,
            snapshot()->size()};
}

ContentStore &contentStore()
{
    static ContentStore store(configuredContentPath());
    return store;
}
}  // namespace blog
