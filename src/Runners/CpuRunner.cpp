#include "CpuRunner.h"

#include <nlohmann/json.hpp>

#include "aura.hpp"
#include "Utils/ColorsDefinitions.h"
#include "Utils/EnhancedJson.h"
#include "Utils/JsonUtils.h"
#include "Utils/AuraUtils.h"
#include "AuraLogger/AuraLogger.h"
#include "AuraAssert/AuraAssert.h"

namespace aura3d {

CpuRunner::CpuRunner(WindowDetails windowDetails)
{
#ifdef SDL_WINDOW_MANAGER
    _windowManagerApi = std::make_unique<aura3d::SDLAuraWindowManager>(windowDetails);
#else
    _windowManagerApi = std::make_unique<aura3d::GlfwAuraWindowManager>(windowDetails);
#endif

    _windowManagerApi->createWindow(APPLICATION_NAME);

    aura3d::CpuFrameBufferManager::Config frameBufferSettings = {};
    frameBufferSettings.width = windowDetails.width;
    frameBufferSettings.height = windowDetails.height;

    _frameBufferManager = std::make_unique<aura3d::CpuFrameBufferManager>(_windowManagerApi->getWindowInstance(), frameBufferSettings);
}

CpuRunner::~CpuRunner()
{
    // Empty
}

void CpuRunner::run()
{
    auto window = _windowManagerApi->getWindowInstance();
    auto windowDetails = _windowManagerApi->getWindowDetails();
    auto windowFLags = _windowManagerApi->getWindowFlags();
    SDL_SetWindowData(window, "CpuFrameBufferManager", _frameBufferManager.get());
    const int centerX = windowDetails->width / 2;
    const int centerY = windowDetails->height / 2;

    // POSITION HISTORIC OF ONE OBJECT
    EnhancedJson rectPoints = JsonUtils::loadFromFile("./test.json");
    AURA_ASSERT(rectPoints.is_array());

    AURA_TRACE << AuraUtils::fast_int_sqrt(121) << " " << AuraUtils::fast_sqrt(256);

    _windowManagerApi->process([&]() {
        if (windowFLags->resized)
        {
            _frameBufferManager->resizeFramebuffer(windowDetails->width, windowDetails->height);
            windowFLags->resized = false;
        }

        // Clear the framebuffer
        _frameBufferManager->clear(aura3d::colors::CORNSILK_UINT32);

        int width = _frameBufferManager->getWidth();
        int height = _frameBufferManager->getHeight();

        // ============= ENHANCED VISUALIZATION CODE =============

        // 1. Group objects by timestamp to analyze movement over time
        std::map<std::string, std::vector<Rectangle>> objectsByTime;

        // 2. Track movement metrics
        float totalDistance = 0.0f;
        float maxSpeed = 0.0f;
        std::string fastestMovementTime;

        // 3. Calculate area coverage
        std::vector<std::pair<int, int>> allPositions;

        // 4. Store previous positions to draw trajectories
        std::vector<Point> previousPositions;
        std::vector<Point> currentPositions;

        // First pass - collect statistics and group by timestamp
        std::string prevTimestamp;
        Point prevCenter{0, 0};
        bool isFirstPoint = true;

        for (size_t i = 0; i < rectPoints.size(); i++) {
            auto position = static_cast<EnhancedJson>(rectPoints[i]).get("position");
            std::string timestamp = position.get<std::string>("timestamp", "");
            float fx = position.get<float>("x", 0.0f);
            float fy = position.get<float>("y", 0.0f);
            float fw = position.get<float>("w", 0.0f);
            float fh = position.get<float>("h", 0.0f);

            // Convert to pixel coordinates
            int x = std::floor(fx * width);
            int y = std::floor(fy * height);
            int w = std::floor(fw * width);
            int h = std::floor(fh * height);

            // Store rectangle in time-based map
            Rectangle rect = {x, y, w, h};
            objectsByTime[timestamp].push_back(rect);

            // Calculate center point of the rectangle
            Point center = {x + w/2, y + h/2};
            currentPositions.push_back(center);

            // // Track all pixel positions for heat map
            // for (int px = x; px < x + w; px += 5) {  // Sample every 5 pixels for performance
            //     for (int py = y; py < y + h; py += 5) {
            //         allPositions.push_back({px, py});
            //     }
            // }

            // Calculate distance for all consecutive positions (regardless of timestamp)
            if (!isFirstPoint) {
                float distance = std::sqrt(
                    std::pow(center.x - prevCenter.x, 2) +
                    std::pow(center.y - prevCenter.y, 2)
                    );

                // Always add to total distance
                totalDistance += distance;
            }

            prevCenter = center;
            prevTimestamp = timestamp;
            isFirstPoint = false;
        }

        // Store current positions for next iteration
        previousPositions = currentPositions;

        // Second pass - Draw the visualization elements

        // 1. Draw base rectangles (with transparency for layering)
        for (const EnhancedJson& box : rectPoints) {
            auto positions = box.get("position");
            float fx = positions.get<float>("x", 0.0f);
            float fy = positions.get<float>("y", 0.0f);
            float fw = positions.get<float>("w", 0.0f);
            float fh = positions.get<float>("h", 0.0f);

            int x = std::floor(fx * width);
            int y = std::floor(fy * height);
            int w = std::floor(fw * width);
            int h = std::floor(fh * height);

            // Use semi-transparent blue (if your implementation supports alpha)
            uint32_t semitransparentBlue = aura3d::colors::BLUE_UINT32 * 0.7;
            _frameBufferManager->drawRect(x, y, w, h, semitransparentBlue);
        }

        // 2. Draw movement trajectories
        for (size_t i = 1; i < previousPositions.size(); i++) {
            Point p1 = previousPositions[i-1];
            Point p2 = previousPositions[i];

            // Use bright green for trajectories
            _frameBufferManager->drawLine(p1.x, p1.y, p2.x, p2.y, aura3d::colors::DARK_GREEN_UINT32);
        }

        // // 3. Generate heat map based on position frequency
        // std::map<std::pair<int, int>, int> positionCounts;
        // for (const auto& pos : allPositions) {
        //     positionCounts[pos]++;
        // }

        // // Find max frequency for normalization
        // int maxCount = 0;
        // for (const auto& pair : positionCounts) {
        //     if (pair.second > maxCount) maxCount = pair.second;
        // }

        // Draw heat map (areas with more frequent visits are more intense)
        // for (const auto& pair : positionCounts) {
        //     int px = pair.first.first;
        //     int py = pair.first.second;
        //     int count = pair.second;

        //     // Normalize intensity (0.0 to 1.0)
        //     float intensity = static_cast<float>(count) / maxCount;

        //     // Create heat color (red for hotspots)
        //     uint32_t heatColor = static_cast<uint32_t>(intensity * 255) << 16; // Red channel

        //     // Draw pixel only if sufficient intensity
        //     if (intensity > 0.3) {
        //         _frameBufferManager->setPixel(px, py, heatColor);
        //     }
        // }

        // 4. Overlay statistical information
        int textY = 20;
        _frameBufferManager->drawText("Object Movement Analysis", 10, textY, aura3d::colors::RED_UINT32);
        textY += 20;

        // Total number of tracked positions
        std::string posCountText = "Total de posicoes: " + std::to_string(rectPoints.size());
        _frameBufferManager->drawText(posCountText, 10, textY, aura3d::colors::RED_UINT32);
        textY += 20;

        // Total movement distance
        std::string distanceText = "Distancia total percorrida: " + std::to_string(static_cast<int>(totalDistance)) + " pixels";
        _frameBufferManager->drawText(distanceText, 10, textY, aura3d::colors::RED_UINT32);
        textY += 20;

        // Time period
        if (!rectPoints.empty()) {
            std::string startTime = static_cast<EnhancedJson>(rectPoints.front()).get<std::string>("timestamp", "");
            std::string endTime = static_cast<EnhancedJson>(rectPoints.back()).get<std::string>("timestamp", "");

            std::tm tmPrev = {}, tmCurrent = {};
            std::istringstream ssPrev(startTime);
            std::istringstream ssCurrent(endTime);
            ssPrev >> std::get_time(&tmPrev, "%d/%m/%Y %H:%M:%S");
            ssCurrent >> std::get_time(&tmCurrent, "%d/%m/%Y %H:%M:%S");
            std::time_t timePrev = std::mktime(&tmPrev);
            std::time_t timeCurrent = std::mktime(&tmCurrent);
            double seconds = std::difftime(timeCurrent, timePrev);

            // Maximum speed
            std::ostringstream stream;
            stream << "Velocidade media: " << std::fixed << std::setprecision(2) << static_cast<double>(totalDistance / seconds) << " px/s at ";
            std::string speedText = stream.str();
            _frameBufferManager->drawText(speedText, 10, textY, aura3d::colors::RED_UINT32);
            textY += 20;

            std::string timeRangeText = "Intervalo: " + startTime + " to " + endTime;
            _frameBufferManager->drawText(timeRangeText, 10, textY, aura3d::colors::RED_UINT32);
        }

        // 5. Draw a mini-map in the corner showing the overall movement path
        int miniMapSize = 150;
        int miniMapX = width - miniMapSize - 10;
        int miniMapY = 10;

        // Draw mini-map background
        _frameBufferManager->drawRect(miniMapX, miniMapY, miniMapSize, miniMapSize, 0x33333333);

        // Draw movement path on mini-map
        for (size_t i = 1; i < previousPositions.size(); i++) {
            // Scale coordinates to fit mini-map
            int x1 = miniMapX + (previousPositions[i-1].x * miniMapSize / width);
            int y1 = miniMapY + (previousPositions[i-1].y * miniMapSize / height);
            int x2 = miniMapX + (previousPositions[i].x * miniMapSize / width);
            int y2 = miniMapY + (previousPositions[i].y * miniMapSize / height);

            // Draw mini-map trajectory line
            _frameBufferManager->drawLine(x1, y1, x2, y2, aura3d::colors::DARK_GREEN_UINT32);
        }

        // Render the final framebuffer
        _frameBufferManager->renderFramebuffer();
    });
}

}
