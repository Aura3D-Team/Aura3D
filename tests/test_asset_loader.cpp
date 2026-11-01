// AssetLoader's background jobs and main-thread callbacks, FileWatcher, and readFileBytes.
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include "aura/Core/AssetLoader.h"
#include "aura/Core/Files.h"

#include "TestUtils.h"

using namespace aura3d;

namespace
{

[[nodiscard]] std::filesystem::path asset(const char *name)
{
    return std::filesystem::path(AURA_TEST_ASSETS_DIR) / name;
}

void test_loader()
{
    AssetLoader loader;
    const std::thread::id self = std::this_thread::get_id();
    bool imageOnSelf = false, imageValid = false, modelMissing = false;
    std::atomic<bool> ranOffThread{false};
    loader.loadImage(asset("valid_texture.png"),
                     [&](const ImageData &image)
                     {
                         imageOnSelf = std::this_thread::get_id() == self;
                         imageValid = image.valid() && image.width == 2;
                     });
    loader.loadModel(asset("missing.glb"),
                     [&](const std::optional<gltf::Model> &model)
                     {
                         modelMissing = !model.has_value();
                     });
    loader.submit(
        [&]() -> std::function<void()>
        {
            ranOffThread = std::this_thread::get_id() != self;
            throw std::runtime_error("a job that fails");
        });
    AURA_CHECK(loader.pending() == 3, "AssetLoader: three jobs pending");
    loader.wait();
    AURA_CHECK(!imageValid && loader.pending() == 3, "AssetLoader: nothing is handed back before poll()");
    AURA_CHECK(loader.poll() == 3 && loader.pending() == 0, "AssetLoader: poll() hands back all three");
    AURA_CHECK(imageValid && imageOnSelf, "AssetLoader: the image decodes off-thread and arrives on the poller");
    AURA_CHECK(modelMissing, "AssetLoader: a missing model arrives as nullopt");
    AURA_CHECK(ranOffThread, "AssetLoader: jobs run on the loader's threads; a throwing one still counts down");

    //! A callback may queue more work, and destruction with work queued neither hangs nor calls it.
    bool chained = false;
    loader.submit(
        [&]() -> std::function<void()>
        {
            return [&]
            {
                loader.submit(
                    [&]() -> std::function<void()>
                    {
                        return [&]
                        {
                            chained = true;
                        };
                    });
            };
        });
    loader.wait();
    loader.poll();
    loader.wait();
    loader.poll();
    AURA_CHECK(chained, "AssetLoader: a callback can submit the next load");

    //! One thread busy, so the rest are still queued when the loader goes: they must not run.
    std::atomic<bool> queuedRan{false};
    {
        AssetLoader single(1);
        single.submit(
            []() -> std::function<void()>
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return {};
            });
        for (int i = 0; i < 3; ++i)
            single.submit(
                [&]() -> std::function<void()>
                {
                    queuedRan = true;
                    return {};
                });
    }
    AURA_CHECK(!queuedRan, "AssetLoader: destruction drops queued work");
}

void test_destroy_with_work()
{
    bool called = false;
    {
        AssetLoader loader(1);
        for (int i = 0; i < 64; ++i)
            loader.loadImage(asset("valid_texture.png"),
                             [&](const ImageData &)
                             {
                                 called = true;
                             });
    }
    AURA_CHECK(!called, "AssetLoader: destroyed with work queued, it joins and calls nothing");
}

void test_watcher()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "aura3d-test-watcher";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");
    const auto write = [&](const char *name, const char *text)
    {
        std::ofstream(root / name) << text;
    };
    write("nested/a.frag", "one");
    FileWatcher watcher(root, std::chrono::milliseconds{0});
    AURA_CHECK(!watcher.changed(), "FileWatcher: the first look is the baseline");
    AURA_CHECK(!watcher.changed(), "FileWatcher: nothing changed");
    std::filesystem::last_write_time(root / "nested/a.frag", std::filesystem::last_write_time(root / "nested/a.frag") +
                                                                 std::chrono::seconds{2});
    AURA_CHECK(watcher.changed() && !watcher.changed(), "FileWatcher: a rewritten file, reported once");
    write("b.vert", "two");
    AURA_CHECK(watcher.changed(), "FileWatcher: a new file");
    std::filesystem::remove(root / "b.vert");
    AURA_CHECK(watcher.changed(), "FileWatcher: a removed file");

    const auto bytes = readFileBytes(root / "nested/a.frag");
    AURA_CHECK(bytes && bytes->size() == 3 && static_cast<char>((*bytes)[0]) == 'o', "readFileBytes: the whole file");
    AURA_CHECK(!readFileBytes(root / "absent"), "readFileBytes: a missing file is nullopt");
    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    test_loader();
    test_destroy_with_work();
    test_watcher();
    return aura3d::test::failureCount() == 0 ? 0 : 1;
}
