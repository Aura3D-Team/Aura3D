#include "aura/Core/AssetLoader.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "aura/aura.h"

namespace aura3d
{

AssetLoader::AssetLoader(u32 threads) : _pool(std::max(threads, 1u))
{
}

//! The pool still runs what is queued; with _stopping set those jobs skip their work and their callbacks.
AssetLoader::~AssetLoader()
{
    _stopping = true;
}

void AssetLoader::submit(std::function<std::function<void()>()> work)
{
    {
        const std::scoped_lock lock(_mutex);
        ++_pending;
        ++_running;
    }
    //! The future is not kept: run() reports through _done.
    static_cast<void>(_pool.submit(
        [this, work = std::move(work)]
        {
            run(work);
        }));
}

void AssetLoader::loadModel(std::filesystem::path path, std::function<void(std::optional<gltf::Model>)> done)
{
    submit(
        [path = std::move(path), done = std::move(done)]() -> std::function<void()>
        {
            //! Shared so the callback stays copyable for std::function while the model is moved, not copied.
            auto model = std::make_shared<std::optional<gltf::Model>>(gltf::load(path));
            return [done, model]
            {
                done(std::move(*model));
            };
        });
}

void AssetLoader::loadImage(std::filesystem::path path, std::function<void(ImageData)> done)
{
    submit(
        [path = std::move(path), done = std::move(done)]() -> std::function<void()>
        {
            auto image = std::make_shared<ImageData>(ImageLoader::loadRGBA(path.string()));
            return [done, image]
            {
                done(std::move(*image));
            };
        });
}

usize AssetLoader::poll()
{
    std::vector<std::function<void()>> ready;
    {
        const std::scoped_lock lock(_mutex);
        ready.swap(_done);
    }
    //! Outside the lock: a callback may submit more work.
    for (const std::function<void()> &callback : ready)
        if (callback)
            callback();
    {
        const std::scoped_lock lock(_mutex);
        _pending -= ready.size();
    }
    return ready.size();
}

usize AssetLoader::pending() const
{
    const std::scoped_lock lock(_mutex);
    return _pending;
}

void AssetLoader::wait()
{
    std::unique_lock lock(_mutex);
    _finished.wait(lock,
                   [this]
                   {
                       return _running == 0;
                   });
}

void AssetLoader::run(const std::function<std::function<void()>()> &work)
{
    std::function<void()> callback;
    if (!_stopping)
    {
        try
        {
            callback = work();
        }
        catch (const std::exception &error)
        {
            INK_ERROR << "AssetLoader: a background job failed: " << error.what();
        }
        catch (...)
        {
            INK_ERROR << "AssetLoader: a background job failed";
        }
    }
    const std::scoped_lock lock(_mutex);
    --_running;
    //! A failed job still comes back, empty, so pending() counts down.
    _done.push_back(std::move(callback));
    _finished.notify_all();
}

} // namespace aura3d
