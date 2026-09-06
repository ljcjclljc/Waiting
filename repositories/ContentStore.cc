#include "ContentStore.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
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

std::string contentFingerprint(const std::filesystem::path &directory)
{
    if (!std::filesystem::is_directory(directory))
        throw std::runtime_error("Content directory not found: " +
                                 directory.string());

    std::vector<std::string> entries;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().extension() != ".md")
            continue;

        const auto timestamp =
            entry.last_write_time().time_since_epoch().count();
        const auto size = entry.is_regular_file() ?
                              std::filesystem::file_size(entry.path()) :
                              0;
        entries.push_back(entry.path().filename().generic_string() + "|" +
                          (entry.is_symlink() ? "link" : "file") + "|" +
                          std::to_string(size) + "|" +
                          std::to_string(timestamp));
    }
    std::sort(entries.begin(), entries.end());

    std::string fingerprint;
    for (const auto &entry : entries)
    {
        fingerprint.append(entry);
        fingerprint.push_back('\n');
    }
    return fingerprint;
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
        activeContentFingerprint_ = contentFingerprint(contentDirectory_);
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
    std::string candidateFingerprint;
    try
    {
        candidateVersion = readVersionFile(versionFile_);
        candidateFingerprint = contentFingerprint(contentDirectory_);
    }
    catch (const std::exception &error)
    {
        std::lock_guard lock(reloadMutex_);
        lastError_ = error.what();
        LOG_ERROR << "Content reload marker rejected: " << lastError_;
        return false;
    }

    std::lock_guard lock(reloadMutex_);
    const bool markerChanged = candidateVersion != observedVersion_;
    const bool contentChanged =
        candidateFingerprint != activeContentFingerprint_;
    if (!markerChanged && !contentChanged)
        return false;
    observedVersion_ = candidateVersion;

    if (!candidateVersion.empty() && !isCommitSha(candidateVersion))
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
        observedVersion_ = candidateVersion;
        activeContentFingerprint_ = std::move(candidateFingerprint);
        if (isCommitSha(candidateVersion))
            activeVersion_ = candidateVersion;
        lastError_.clear();
        LOG_INFO << "Content reloaded after "
                 << (markerChanged ? "version marker change" :
                                     "Markdown directory change");
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
