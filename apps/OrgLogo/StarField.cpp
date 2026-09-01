#include "StarField.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace orglogo {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

//! Seeded across a disc large enough to cover the corners of a wide window,
//! so the field stays full as it turns.
constexpr int kStarCount = 760;
constexpr float kFieldInnerRadius = 0.17f;
constexpr float kFieldOuterRadius = 0.95f;

//! Radians per second for the middle depth layer. Slow enough to read as drift
//! rather than motion; over a minute the sky turns about half a degree.
constexpr float kFieldSpinRate = 0.010f;

//! Depth layers: how much of the drift each takes, and how near it reads.
constexpr std::array<float, 3> kLayerSpin{0.62f, 1.00f, 1.48f};
constexpr std::array<float, 3> kLayerDepth{0.74f, 1.00f, 1.26f};

//! Fraction of a flare's period spent flaring.
constexpr float kFlareDuty = 0.12f;

//! Above this luminance a star also gets a small white-hot core quad, so the
//! brightest stars whiten at the centre while keeping their tint at the edges.
constexpr float kCoreThreshold = 0.72f;

constexpr std::size_t kMaxMeteors = 2;

/// Wash-out: stars fade as they approach the orb, the way faint ones vanish
/// beside anything genuinely bright.
[[nodiscard]] float orbWashout(float radius) noexcept
{
    return smoothstep(0.13f, 0.32f, radius);
}

} // namespace

StarField::StarField(aura3d::IRenderer& renderer, std::uint32_t seed)
    : _rng(seed ^ 0x9E3779B9u)
{
    const StarAtlas atlas = bakeStarAtlas();
    _atlas = renderer.createTextureFromPixels(atlas.image.pixels.data(),
                                              static_cast<u32>(atlas.image.width),
                                              static_cast<u32>(atlas.image.height));
    _softUv = atlas.soft;
    _spikedUv = atlas.spiked;
    _cometUv = atlas.comet;

    _seedStars(seed);

    //! Worst case is every star drawing a sprite and a core, plus the meteors.
    const std::size_t maxQuads = _stars.size() * 2u + kMaxMeteors;
    _vertices.reserve(maxQuads * 4u);
    _indices.reserve(maxQuads * 6u);

    _meteors.reserve(kMaxMeteors);
    _meteorCountdown = 3.0f;
}

void StarField::_seedStars(std::uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    _stars.reserve(kStarCount);

    for (int i = 0; i < kStarCount; ++i)
    {
        Star star;

        /*
         * Area-uniform over the annulus. Sampling the radius uniformly instead
         * would crowd the stars toward the centre, which is exactly where the
         * orb needs the sky to be emptiest.
         */
        const float t = unit(rng);
        star.radius = std::sqrt(lerp(kFieldInnerRadius * kFieldInnerRadius,
                                     kFieldOuterRadius * kFieldOuterRadius, t));
        star.theta = unit(rng) * kTwoPi;

        const auto layer = static_cast<std::size_t>(
            std::min(2, static_cast<int>(unit(rng) * 3.0f)));
        star.spinScale = kLayerSpin[layer];
        const float depth = kLayerDepth[layer];

        //! Brightness biased hard toward dim, so only a handful of stars carry
        //! the eye and the rest are texture.
        const float roll = std::pow(unit(rng), 2.2f);
        star.brightness = (0.10f + roll * 0.95f) * depth;
        star.size = (0.0055f + std::pow(unit(rng), 2.0f) * 0.0175f) * depth;
        star.spiked = star.brightness > 0.62f;

        //! Mostly white, some blue-white, a few warm.
        const float temperature = unit(rng);
        star.tint = temperature < 0.32f ? kStarBlue
                  : (temperature > 0.86f ? kStarWarm : kStarWhite);

        star.twinklePhase = unit(rng) * kTwoPi;
        star.twinkleRate = 0.50f + unit(rng) * 2.20f;

        //! One flare every 11 to 33 seconds, out of phase with every other
        //! star, so the sky never flashes in unison.
        star.flareRate = 0.030f + unit(rng) * 0.060f;
        star.flarePhase = unit(rng);

        star.roll = unit(rng) * kTwoPi;
        star.rollRate = (unit(rng) - 0.5f) * 0.045f;

        _stars.push_back(star);
    }
}

void StarField::update(float time, float deltaTime, float orbPulse)
{
    _time = time;
    _orbPulse = orbPulse;

    for (Meteor& meteor : _meteors)
        meteor.age += deltaTime;

    std::erase_if(_meteors, [](const Meteor& meteor) noexcept {
        return meteor.age >= meteor.life;
    });

    _meteorCountdown -= deltaTime;
    if (_meteorCountdown <= 0.0f)
    {
        if (_meteors.size() < kMaxMeteors)
            _spawnMeteor();

        std::uniform_real_distribution<float> gap(5.0f, 12.0f);
        _meteorCountdown = gap(_rng);
    }
}

void StarField::_spawnMeteor()
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    Meteor meteor;

    //! Enters just off-screen and crosses toward the far side, aimed off the
    //! centre so it never runs straight through the orb.
    const float entry = unit(_rng) * kTwoPi;
    meteor.origin = {std::cos(entry) * 1.15f, std::sin(entry) * 1.15f};

    const float heading = entry + kPi + (unit(_rng) - 0.5f) * 1.10f;
    const float speed = 0.75f + unit(_rng) * 0.70f;
    meteor.velocity = {std::cos(heading) * speed, std::sin(heading) * speed};

    meteor.life = 2.40f / speed;
    meteor.length = 0.10f + unit(_rng) * 0.15f;
    meteor.width = 0.013f + unit(_rng) * 0.010f;
    meteor.brightness = 0.60f + unit(_rng) * 0.40f;

    _meteors.push_back(meteor);
}

void StarField::submit(aura3d::IRenderer& renderer, const SceneLayout& layout)
{
    _buildBatch(layout);

    if (_indices.empty() || !aura3d::isValidHandle(_atlas))
        return;

    renderer.drawBatch2D(_vertices, _indices, _atlas);
}

void StarField::_buildBatch(const SceneLayout& layout)
{
    //! clear() keeps the capacity, so a frame's batch allocates nothing.
    _vertices.clear();
    _indices.clear();

    for (const Star& star : _stars)
        _appendStar(star, layout);

    for (const Meteor& meteor : _meteors)
        _appendMeteor(meteor, layout);
}

void StarField::_appendStar(const Star& star, const SceneLayout& layout)
{
    const float angle = star.theta + _time * kFieldSpinRate * star.spinScale;

    //! The field breathes with the core, by a fraction of a percent -- enough
    //! to couple the two, far too little to read as the sky moving.
    const float radius = star.radius * (1.0f + 0.006f * _orbPulse);

    const glm::vec2 position = layout.center
                             + glm::vec2{std::cos(angle), std::sin(angle)} * (radius * layout.unit);

    /*
     * Twinkle: two sines whose rates are not in an integer ratio, so the
     * pattern does not repeat over any span an observer would notice.
     */
    const float twinkle = 0.60f
                        + 0.28f * std::sin(_time * star.twinkleRate + star.twinklePhase)
                        + 0.12f * std::sin(_time * star.twinkleRate * 1.73f + star.twinklePhase * 2.1f);

    //! Flare: a rare, smooth swell, on its own much slower clock.
    float flare = 0.0f;
    const float flareClock = std::fmod(_time * star.flareRate + star.flarePhase, 1.0f);
    if (flareClock < kFlareDuty)
    {
        const float progress = flareClock / kFlareDuty;
        const float bump = 4.0f * progress * (1.0f - progress);
        flare = bump * bump;
    }

    float luminance = star.brightness * (twinkle + 1.15f * flare);
    luminance *= orbWashout(radius);
    if (luminance <= 0.01f)
        return;

    const float halfExtent = star.size * (0.80f + 0.35f * twinkle + 0.55f * flare) * layout.unit;

    //! Rotation can push a corner out by up to sqrt(2) of the half-extent.
    const float margin = halfExtent * 1.45f;
    if (position.x + margin < 0.0f || position.x - margin > layout.windowSize.x ||
        position.y + margin < 0.0f || position.y - margin > layout.windowSize.y)
        return;

    //! Scintillation: a flaring star whitens, and the twinkle shifts its
    //! colour slightly, the way atmospheric dispersion does to a real one.
    Rgb tint = mix(star.tint, kStarWhite, 0.40f * flare);
    tint.b *= 0.94f + 0.10f * twinkle;

    const float alpha = clamp01(luminance);
    appendQuad(_vertices, _indices, position, {halfExtent, halfExtent},
               star.roll + _time * star.rollRate,
               star.spiked ? _spikedUv : _softUv,
               glm::vec4{tint.r, tint.g, tint.b, alpha});

    //! White-hot centre for the brightest stars only. A second, much smaller
    //! quad is what lets a star be blue at its edges and white at its core,
    //! which one tinted sprite cannot express on its own.
    if (luminance > kCoreThreshold)
    {
        const float coreAlpha = clamp01((luminance - kCoreThreshold) * 2.6f);
        appendQuad(_vertices, _indices, position,
                   {halfExtent * 0.34f, halfExtent * 0.34f}, 0.0f, _softUv,
                   glm::vec4{1.0f, 1.0f, 1.0f, coreAlpha});
    }
}

void StarField::_appendMeteor(const Meteor& meteor, const SceneLayout& layout)
{
    const float progress = meteor.life > 0.0f ? meteor.age / meteor.life : 1.0f;

    //! Snaps into view, lingers, then fades: a symmetric envelope reads as a
    //! light being dimmed rather than as something moving past.
    const float envelope = smoothstep(0.0f, 0.10f, progress)
                         * (1.0f - smoothstep(0.62f, 1.0f, progress));
    if (envelope <= 0.01f)
        return;

    const glm::vec2 head = meteor.origin + meteor.velocity * meteor.age;
    const float speed = glm::length(meteor.velocity);
    if (speed <= 0.0f)
        return;

    const glm::vec2 direction = meteor.velocity / speed;
    const float halfLength = meteor.length * 0.5f * layout.unit;
    const float halfWidth = meteor.width * 0.5f * layout.unit;

    //! The sprite's head sits at its +U edge, so the quad's centre trails the
    //! head by half its length along the direction of travel.
    const glm::vec2 center = layout.center + head * layout.unit - direction * halfLength;

    const float margin = halfLength + halfWidth;
    if (center.x + margin < 0.0f || center.x - margin > layout.windowSize.x ||
        center.y + margin < 0.0f || center.y - margin > layout.windowSize.y)
        return;

    const Rgb tint = mix(kStarWhite, kStarBlue, 0.55f);
    const float alpha = clamp01(meteor.brightness * envelope);

    appendQuad(_vertices, _indices, center, {halfLength, halfWidth},
               std::atan2(direction.y, direction.x), _cometUv,
               glm::vec4{tint.r, tint.g, tint.b, alpha});
}

} // namespace orglogo
