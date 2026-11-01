#ifndef AURA_CORE_FILES_H
#define AURA_CORE_FILES_H

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace aura3d
{

/// The whole of @p path; std::nullopt when it cannot be read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFileBytes(const std::filesystem::path &path);

/**
 * @brief Tells when any file under a directory was added, removed or rewritten.
 *
 * Polls modification times, at most once per @p interval however often changed() is asked, so it can sit in a
 * frame loop.
 */
class FileWatcher
{
  public:
    explicit FileWatcher(std::filesystem::path root,
                         std::chrono::milliseconds interval = std::chrono::milliseconds{500});

    /// True once per change since the last true. The first call only takes the baseline.
    [[nodiscard]] bool changed();

  private:
    [[nodiscard]] std::map<std::filesystem::path, std::filesystem::file_time_type> scan() const;

    std::filesystem::path _root;
    std::chrono::milliseconds _interval;
    std::chrono::steady_clock::time_point _nextScan{};
    std::optional<std::map<std::filesystem::path, std::filesystem::file_time_type>> _seen;
};

} // namespace aura3d

#endif // AURA_CORE_FILES_H
