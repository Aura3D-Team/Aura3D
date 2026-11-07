#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <wma/managers/IWindowManager.hpp>

#include "aura/Core/JobSystem/JobSystem.h"

namespace aura3d
{
namespace cpu
{

namespace
{

//! Bands per worker, and the band height's bounds: thinner balances better, taller splits fewer
//! small triangles across band boundaries, where each piece pays the full setup.
constexpr i32 kBandsPerWorker = 4;
constexpr i32 kMinBandRows = 4;
constexpr i32 kMaxBandRows = 64;
//! Below this many queued triangles one thread bins faster than a pool wake-up.
constexpr u32 kParallelBinning = 16384;

} // namespace

i32 CpuFrameBufferManager::paddedStride(i32 width) noexcept
{
    return (std::max(width, 0) + kRowAlignment - 1) / kRowAlignment * kRowAlignment;
}

CpuFrameBufferManager::CpuFrameBufferManager(wma::IWindowManager &windowManager, Config config, const JobSystem &jobs)
    : settings(config), _stride(paddedStride(config.width)),
      _color(static_cast<size_t>(_stride) * static_cast<size_t>(std::max(config.height, 0)), 0),
      _depth(_color.size(), 1.0f), _windowManager(&windowManager), _jobs(&jobs), _workerCount(jobs.workerCount())
{
    updateBandRows();
}

CpuFrameBufferManager::CpuFrameBufferManager(Config config, const JobSystem &jobs)
    : settings(config), _stride(paddedStride(config.width)),
      _color(static_cast<size_t>(_stride) * static_cast<size_t>(std::max(config.height, 0)), 0),
      _depth(_color.size(), 1.0f), _windowManager(nullptr), _jobs(&jobs), _workerCount(jobs.workerCount())
{
    updateBandRows();
}

void CpuFrameBufferManager::readColor(std::span<u32> out)
{
    const auto width = static_cast<size_t>(std::max(settings.width, 0));
    if (out.size() != width * static_cast<size_t>(std::max(settings.height, 0)))
        return;
    resolvePendingClear();
    for (i32 y = 0; y < settings.height; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * width,
                    _color.data() + static_cast<size_t>(y) * static_cast<size_t>(_stride), width * sizeof(u32));
}

void CpuFrameBufferManager::clear(u32 color)
{
    _clearColor = color;
    _clearPending = true;
}

void CpuFrameBufferManager::clearRows(i32 yStart, i32 yEnd) noexcept
{
    const size_t first = static_cast<size_t>(yStart) * static_cast<size_t>(_stride);
    const size_t count = static_cast<size_t>(yEnd - yStart) * static_cast<size_t>(_stride);
    std::fill_n(_color.data() + first, count, _clearColor);
    std::fill_n(_depth.data() + first, count, 1.0f);
}

void CpuFrameBufferManager::resolvePendingClear()
{
    if (!_clearPending)
        return;
    _clearPending = false;
    dispatchRowBands(
        [this](i32, i32 yStart, i32 yEnd)
        {
            clearRows(yStart, yEnd);
        });
}

bool CpuFrameBufferManager::renderFramebuffer()
{
    if (_windowManager == nullptr)
        return false;

    const wma::SoftwareFramebuffer target = _windowManager->lockFramebuffer();
    if (!target.valid())
        return false;
    resolvePendingClear();

    // The surface can briefly disagree with the plane after a resize; the excess is black.
    const i32 planeWidth = settings.width;
    const i32 planeHeight = settings.height;
    const size_t copyWidth = static_cast<size_t>(std::clamp(std::min(planeWidth, target.width), 0, target.width));
    const size_t surfaceWidth = static_cast<size_t>(target.width);
    const u32 *plane = _color.data();

    const auto copyRows = [&](i32 yStart, i32 yEnd)
    {
        for (i32 y = yStart; y < yEnd; ++y)
        {
            auto *row =
                reinterpret_cast<u32 *>(static_cast<u8 *>(target.pixels) + static_cast<size_t>(y) * target.pitch);
            const size_t copied = y < planeHeight ? copyWidth : 0;
            std::memcpy(row, plane + static_cast<size_t>(y) * static_cast<size_t>(_stride), copied * sizeof(u32));
            std::fill(row + copied, row + surfaceWidth, 0u);
        }
    };
    //! By reference: dispatch() joins before returning, and a std::ref fits std::function without allocating.
    _jobs->dispatch(target.height, std::ref(copyRows));

    _windowManager->presentFramebuffer();
    return true;
}

//! Only the planes: lockFramebuffer() already returns the surface at the window's current size.
void CpuFrameBufferManager::resizeFramebuffer(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        INK_ERROR << "Error: Invalid framebuffer dimensions (" << width << "x" << height << ")";
        return;
    }
    if (width == settings.width && height == settings.height)
        return;

    settings.width = width;
    settings.height = height;
    _stride = paddedStride(width);
    const size_t count = static_cast<size_t>(_stride) * static_cast<size_t>(height);
    _color.resize(count, 0);
    _depth.resize(count, 1.0f);
    updateBandRows();
}

/**
 * Checks if coordinates are within the framebuffer bounds
 * @param p Point to check
 * @return true if coordinates are valid, false otherwise
 */
bool CpuFrameBufferManager::isInsideBounds(Point p) const
{
    return p.x >= 0 && p.x < settings.width && p.y >= 0 && p.y < settings.height;
}

/**
 * Sets a pixel color at the specified coordinates, leaving depth unchanged.
 * @param p Point coordinates
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixel(Point p, u32 color)
{
    if (!isInsideBounds(p))
        return;
    if (_clearPending)
        resolvePendingClear();
    _color[static_cast<size_t>(p.y) * static_cast<size_t>(_stride) + static_cast<size_t>(p.x)] = color;
}

/**
 * Gets the Pixel object (color and depth) at the specified coordinates
 * @param p Point coordinate
 * @return Pixel object, or a default black pixel if coordinates are invalid
 */
Pixel CpuFrameBufferManager::getPixel(Point p) const
{
    if (!isInsideBounds(p))
        return Pixel{0, 1.0f};
    if (_clearPending)
        return Pixel{_clearColor, 1.0f};
    const size_t index = static_cast<size_t>(p.y) * static_cast<size_t>(_stride) + static_cast<size_t>(p.x);
    return Pixel{_color[index], _depth[index]};
}

i32 CpuFrameBufferManager::bandCount() const noexcept
{
    return (std::max(settings.height, 0) + _bandRows - 1) / _bandRows;
}

void CpuFrameBufferManager::updateBandRows() noexcept
{
    const i32 bands = std::max(_workerCount, 1) * kBandsPerWorker;
    const i32 rows = std::clamp((std::max(settings.height, 0) + bands - 1) / bands, kMinBandRows, kMaxBandRows);
    if (rows == _bandRows)
        return;
    _bandRows = rows;
    for (u32 t = 0; t < _queuedCount; ++t)
        if (_queuedBands[t].first <= _queuedBands[t].last)
            _queuedBands[t] = bandsOf(_queuedTriangles[t]);
}

void CpuFrameBufferManager::dispatchRowBands(const std::function<void(i32 band, i32 yStart, i32 yEnd)> &rasterizeBand)
{
    const i32 bands = bandCount();
    if (bands == 0)
        return;

    const i32 height = settings.height;
    std::atomic<i32> nextBand{0};
    const auto work = [&](i32, i32)
    {
        for (i32 band = nextBand.fetch_add(1, std::memory_order_relaxed); band < bands;
             band = nextBand.fetch_add(1, std::memory_order_relaxed))
            rasterizeBand(band, band * _bandRows, std::min(height, (band + 1) * _bandRows));
    };
    //! One item per worker; each then drains the shared counter. By reference, so nothing allocates.
    _jobs->dispatch(std::min(_workerCount, bands), std::ref(work));
}

namespace
{

//! Degenerate or nonfinite triangles are rejected once, at queue time, so no band repeats the checks.
[[nodiscard]] bool acceptable(const ScreenTriangle &triangle) noexcept
{
    const f32 area2 = signedArea2(triangle.v0, triangle.v1, triangle.v2);
    if (!(std::abs(area2) >= 1e-6f))
        return false;
    //! Behind the camera.
    return triangle.v0.invW > 0 && triangle.v1.invW > 0 && triangle.v2.invW > 0;
}

} // namespace

CpuFrameBufferManager::BandSpan CpuFrameBufferManager::bandsOf(const ScreenTriangle &triangle) const noexcept
{
    //! Truncation is floor for the non-negative rows kept; rows above the frame and NaN map to band 0.
    const f32 perRow = 1.0f / static_cast<f32>(_bandRows);
    const auto bandOf = [perRow](f32 y)
    {
        constexpr f32 kLastBand = static_cast<f32>(1 << 20);
        const f32 band = y * perRow;
        return band > 0 ? static_cast<i32>(std::min(band, kLastBand)) : 0;
    };
    return {bandOf(std::min({triangle.v0.y, triangle.v1.y, triangle.v2.y})),
            bandOf(std::max({triangle.v0.y, triangle.v1.y, triangle.v2.y}))};
}

void CpuFrameBufferManager::queueTriangle(const ScreenTriangle &triangle, const Texture *texture, RasterMode mode)
{
    if (!acceptable(triangle))
        return;

    if (_queuedBatches.empty() || _queuedBatches.back().texture != texture || _queuedBatches.back().mode != mode)
        _queuedBatches.push_back({texture, mode, _queuedCount, 0});
    if (_queuedCount == _queuedTriangles.size())
    {
        _queuedTriangles.emplace_back();
        _queuedBands.emplace_back();
    }
    _queuedTriangles[_queuedCount] = triangle;
    _queuedBands[_queuedCount] = bandsOf(triangle);
    ++_queuedCount;
    ++_queuedBatches.back().count;
}

void CpuFrameBufferManager::queueScreenTriangles(std::span<const gfx::BatchVertex> vertices,
                                                 std::span<const u32> indices, const Texture *texture)
{
    const u32 count = static_cast<u32>(indices.size() / 3);
    if (count == 0)
        return;

    const u32 first = _queuedCount;
    if (_queuedTriangles.size() < first + count)
    {
        _queuedTriangles.resize(first + count);
        _queuedBands.resize(first + count);
    }
    if (_queuedBatches.empty() || _queuedBatches.back().texture != texture ||
        _queuedBatches.back().mode != RasterMode::Batch)
        _queuedBatches.push_back({texture, RasterMode::Batch, first, 0});
    _queuedBatches.back().count += count;
    _queuedCount += count;

    //! Each triangle owns its slot, so bands of the list convert without synchronisation.
    const auto convert = [&](u32 begin, u32 end)
    {
        const auto toScreen = [](const gfx::BatchVertex &v) -> ScreenVertex
        {
            return {v.pos.x, v.pos.y, v.pos.z, 1, v.texCoord, v.color};
        };
        for (u32 t = begin; t < end; ++t)
        {
            const usize corner = usize{t} * 3;
            const u32 i0 = indices[corner], i1 = indices[corner + 1], i2 = indices[corner + 2];
            ScreenTriangle &triangle = _queuedTriangles[first + t];
            BandSpan &bands = _queuedBands[first + t];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                bands = {};
                continue;
            }
            triangle = {toScreen(vertices[i0]), toScreen(vertices[i1]), toScreen(vertices[i2])};
            bands = acceptable(triangle) ? bandsOf(triangle) : BandSpan{};
        }
    };

    //! Below this a pool wake-up costs more than the conversion it spreads.
    constexpr u32 kParallelTriangles = 4096;
    if (count < kParallelTriangles)
        convert(0, count);
    else
        _jobs->dispatch(static_cast<i32>(count),
                        [&](i32 begin, i32 end)
                        {
                            convert(static_cast<u32>(begin), static_cast<u32>(end));
                        });
}

void CpuFrameBufferManager::binTriangles(i32 bands)
{
    const u32 count = _queuedCount;
    const i32 slices = count >= kParallelBinning ? std::max(_workerCount, 1) : 1;
    const auto bandCountU = static_cast<size_t>(bands);
    _binCursors.assign(static_cast<size_t>(slices) * bandCountU, 0);

    const auto sliceBegin = [count, slices](i32 slice)
    {
        return static_cast<u32>(static_cast<u64>(count) * static_cast<u64>(slice) / static_cast<u64>(slices));
    };
    // Per slice, so a slice's entries land contiguously and in order within each band.
    const auto countSlice = [&](i32 slice)
    {
        u32 *counts = _binCursors.data() + static_cast<size_t>(slice) * bandCountU;
        for (u32 t = sliceBegin(slice), end = sliceBegin(slice + 1); t < end; ++t)
            for (i32 band = _queuedBands[t].first, last = std::min(_queuedBands[t].last, bands - 1); band <= last;
                 ++band)
                ++counts[band];
    };
    const auto fillSlice = [&](i32 slice)
    {
        u32 *cursors = _binCursors.data() + static_cast<size_t>(slice) * bandCountU;
        for (u32 t = sliceBegin(slice), end = sliceBegin(slice + 1); t < end; ++t)
            for (i32 band = _queuedBands[t].first, last = std::min(_queuedBands[t].last, bands - 1); band <= last;
                 ++band)
                _binned[cursors[band]++] = t;
    };
    const auto forEachSlice = [&](const auto &body)
    {
        const auto run = [&](i32 begin, i32 end)
        {
            for (i32 slice = begin; slice < end; ++slice)
                body(slice);
        };
        if (slices == 1)
            run(0, 1);
        else
            _jobs->dispatch(slices, std::ref(run));
    };

    forEachSlice(countSlice);
    //! Band-major prefix sum: band b's entries are slice 0's, then slice 1's, and so on.
    _bandFirst.resize(bandCountU + 1);
    u32 total = 0;
    for (size_t band = 0; band < bandCountU; ++band)
    {
        _bandFirst[band] = total;
        for (size_t slice = 0; slice < static_cast<size_t>(slices); ++slice)
        {
            u32 &cursor = _binCursors[slice * bandCountU + band];
            const u32 entries = cursor;
            cursor = total;
            total += entries;
        }
    }
    _bandFirst[bandCountU] = total;
    if (_binned.size() < total)
        _binned.resize(total);
    forEachSlice(fillSlice);
}

void CpuFrameBufferManager::flush()
{
    if (_queuedCount == 0 || bandCount() == 0)
    {
        _queuedCount = 0;
        _queuedBatches.clear();
        resolvePendingClear();
        return;
    }

    binTriangles(bandCount());
    const bool clearFirst = _clearPending;
    _clearPending = false;
    dispatchRowBands(
        [this, clearFirst](i32 band, i32 yStart, i32 yEnd)
        {
            if (clearFirst)
                clearRows(yStart, yEnd);
            //! Entries ascend, so the batch holding each one is found by walking the batch list once.
            const QueuedBatch *batch = _queuedBatches.data();
            bool depthCleared = clearFirst;
            for (u32 entry = _bandFirst[static_cast<size_t>(band)], end = _bandFirst[static_cast<size_t>(band) + 1];
                 entry < end; ++entry)
            {
                const u32 index = _binned[entry];
                while (index >= batch->first + batch->count)
                    ++batch;
                rasterizeTriangle(_queuedTriangles[index], batch->texture, batch->mode, yStart, yEnd, depthCleared);
                depthCleared &= batch->mode != RasterMode::Scene;
            }
        });

    //! The queue keeps its storage: the next frame reuses it without constructing anything.
    _queuedCount = 0;
    _queuedBatches.clear();
}

} // namespace cpu
} // namespace aura3d
