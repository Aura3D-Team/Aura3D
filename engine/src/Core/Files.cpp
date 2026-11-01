#include "aura/Core/Files.h"

#include <fstream>
#include <system_error>
#include <utility>

namespace aura3d
{

std::optional<std::vector<std::byte>> readFileBytes(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::nullopt;
    const std::streamsize size = file.tellg();
    if (size < 0)
        return std::nullopt;
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
        return std::nullopt;
    return bytes;
}

FileWatcher::FileWatcher(std::filesystem::path root, std::chrono::milliseconds interval)
    : _root(std::move(root)), _interval(interval)
{
}

bool FileWatcher::changed()
{
    const auto now = std::chrono::steady_clock::now();
    if (_seen && now < _nextScan)
        return false;
    _nextScan = now + _interval;
    auto current = scan();
    const bool different = _seen && *_seen != current;
    _seen = std::move(current);
    return different;
}

std::map<std::filesystem::path, std::filesystem::file_time_type> FileWatcher::scan() const
{
    //! error_code overloads throughout: a file vanishing mid-scan (a build restaging it) is a change, not an error.
    std::map<std::filesystem::path, std::filesystem::file_time_type> times;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(_root, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            times[it->path()] = it->last_write_time(error);
    return times;
}

} // namespace aura3d
