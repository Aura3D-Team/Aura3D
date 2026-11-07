#ifndef AURA_ASSET_LOADER_H
#define AURA_ASSET_LOADER_H

#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include <ink/ThreadPool.h>

#include "aura/Core/Gltf/Gltf.h"
#include "aura/Core/ImageLoader/ImageLoader.h"

namespace aura3d
{

/**
 * @brief Reads and decodes assets on background threads; hands each result back on the thread that polls.
 *
 * File reading, glTF parsing and image decoding happen off the frame. Their callbacks run inside poll(), on the
 * caller's thread, which is where GPU uploads (createTexture(), uploadModel()) are allowed:
 *
 * @code
 *   AssetLoader loader;
 *   loader.loadModel("robot.glb", [&](std::optional<gltf::Model> model) {
 *       if (model)
 *           robot = uploadModel(renderer, *model);
 *   });
 *   // each frame:
 *   loader.poll();
 * @endcode
 *
 * Destroying the loader finishes the work already running, drops the rest and never calls their callbacks.
 */
class AssetLoader
{
  public:
    explicit AssetLoader(u32 threads = 2);
    ~AssetLoader();
    AssetLoader(const AssetLoader &) = delete;
    AssetLoader &operator=(const AssetLoader &) = delete;
    AssetLoader(AssetLoader &&) = delete;
    AssetLoader &operator=(AssetLoader &&) = delete;

    /// Runs @p work on a loader thread; the callback it returns runs in a later poll(). An exception from @p work
    /// is logged and its callback skipped.
    void submit(std::function<std::function<void()>()> work);

    /// gltf::load() in the background; @p done gets its result.
    void loadModel(std::filesystem::path path, std::function<void(std::optional<gltf::Model>)> done);
    /// ImageLoader::loadRGBA() in the background; @p done gets the image, invalid if it could not be read.
    void loadImage(std::filesystem::path path, std::function<void(ImageData)> done);

    /// Calls the callbacks of everything finished since the last poll, in completion order; returns how many.
    usize poll();
    /// Submitted and not yet handed back by poll().
    [[nodiscard]] usize pending() const;
    /// Blocks until every submitted job has finished on its thread (their callbacks still wait for poll()).
    void wait();

  private:
    void run(const std::function<std::function<void()>()> &work);

    mutable std::mutex _mutex;
    std::condition_variable _finished;
    std::vector<std::function<void()>> _done;
    usize _running = 0; //! Submitted and not yet finished on a pool thread.
    usize _pending = 0;
    std::atomic<bool> _stopping = false;
    //! Last, so it is destroyed first: it drains its queue, and those jobs still reach the members above.
    ink::ThreadPool _pool;
};

} // namespace aura3d

#endif // AURA_ASSET_LOADER_H
