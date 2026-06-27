#include "ImageSource.h"

#include "AppConfig.h"
#include "DebugLog.h"
#include "SharedSd.h"

#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

using namespace esp_panel::board;

namespace
{
struct DynamicAnimation
{
    String animPath;
    String label;
    size_t frameCount = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t canvasWidth = 0;
    uint16_t canvasHeight = 0;
    uint16_t offsetX = 0;
    uint16_t offsetY = 0;
    uint8_t scale = 1;
    String categoryPath;
    size_t categoryEntryCount = 0;
    struct Frame
    {
        String file;
        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t offsetX = 0;
        uint16_t offsetY = 0;
        uint16_t canvasWidth = 0;
        uint16_t canvasHeight = 0;
        uint8_t scale = 1;
        uint32_t dataOffset = 0;
        uint32_t dataSize = 0;
    };
    std::vector<Frame> frames;
    bool animContainer = false;
};

struct CachedAnimationEntry
{
    String path;
    String label;
};

struct CachedAnimationCategory
{
    String path;
    bool scanned = false;
    std::vector<CachedAnimationEntry> entries;
    size_t lastRandomIndex = static_cast<size_t>(-1);
};

enum class DynamicPickMode
{
    Random,
    Sequential
};

constexpr uint32_t kSdRetryMs = 30UL * 1000UL;
constexpr size_t kReadChunkBytes = 4096;
constexpr size_t kPersistentFrameBufferPixels = static_cast<size_t>(AppConfig::kScreenWidth) * AppConfig::kScreenHeight;
constexpr uint32_t kSpecialMinIntervalMs = 3UL * 60UL * 1000UL;
constexpr uint32_t kSpecialJitterMs = 20UL * 1000UL;
constexpr uint8_t kLoopsBeforeChange = 2;
constexpr uint8_t kAnimMagic[8] = {'R', '2', 'A', 'N', 'I', 'M', '0', '1'};
constexpr uint16_t kAnimHeaderBytes = 28;
constexpr uint16_t kAnimFrameEntryBytes = 20;
constexpr uint16_t kAnimFrameFlagScale2x = 0x0001;
constexpr uint16_t kAnimFrameFlagScale4x = 0x0002;

Board* g_board = nullptr;
bool g_demoLoaded = false;
ImageSource::Mood g_requestedMood = ImageSource::Mood::Neutral;
DynamicAnimation g_dynamicActive;
bool g_activeDynamic = false;
bool g_oneShotActive = false;
size_t g_dynamicSpecialIndex = 0;
std::vector<CachedAnimationCategory> g_animationCache;
uint16_t* g_frameBuffer = nullptr;
size_t g_frameBufferPixels = 0;
size_t g_loadedFrameIndex = static_cast<size_t>(-1);
uint32_t g_frameIntervalMs = AppConfig::kImageFrameIntervalMs;
uint32_t g_nextFrameMs = 0;
uint32_t g_nextSdRetryMs = 0;
uint32_t g_nextSpecialMs = 0;
uint8_t g_baseLoopCount = 0;
char g_detail[80] = "";
String g_errorText;
File g_animFile;
String g_animFilePath;

bool failBegin(const char* errorText)
{
    if (errorText != nullptr && errorText[0] != '\0') {
        g_errorText = errorText;
    }
    g_nextSdRetryMs = millis() + kSdRetryMs;
    return false;
}

bool ensureFrameBuffer(size_t pixelCount)
{
    if (pixelCount == 0) {
        return false;
    }
    if (pixelCount > kPersistentFrameBufferPixels) {
        g_errorText = String("FRAME BIG ") + String(static_cast<unsigned>(pixelCount * sizeof(uint16_t)));
        return false;
    }
    if (g_frameBuffer != nullptr && g_frameBufferPixels >= pixelCount) {
        return true;
    }

    const size_t reservePixels = kPersistentFrameBufferPixels;
    const size_t byteCount = reservePixels * sizeof(uint16_t);
    g_frameBuffer = static_cast<uint16_t*>(heap_caps_malloc(byteCount,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_frameBuffer == nullptr) {
        g_frameBuffer = static_cast<uint16_t*>(heap_caps_malloc(byteCount, MALLOC_CAP_8BIT));
    }
    if (g_frameBuffer == nullptr) {
        g_errorText = String("NO MEM ") + String(static_cast<unsigned>(byteCount));
        return false;
    }

    g_frameBufferPixels = reservePixels;
    return true;
}

bool loadFrame(size_t frameIndex);

uint32_t randomModulo(uint32_t maxValue)
{
    return maxValue == 0 ? 0 : (esp_random() % maxValue);
}

uint16_t readLe16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0])
        | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0])
        | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16)
        | (static_cast<uint32_t>(data[3]) << 24);
}

String baseNameWithoutExt(const String& path)
{
    String name(path);
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) {
        name = name.substring(slash + 1);
    }
    const int dot = name.lastIndexOf('.');
    if (dot > 0) {
        name = name.substring(0, dot);
    }
    return name;
}

String animationLabelFromPath(const String& path)
{
    String normalized(path);
    if (normalized.endsWith("/animation.anim")) {
        normalized = normalized.substring(0, normalized.length() - strlen("/animation.anim"));
    }
    return baseNameWithoutExt(normalized);
}

bool isAnimFileName(const String& name)
{
    String lower(name);
    lower.toLowerCase();
    return lower.endsWith(".anim");
}

void clearAnimationCache()
{
    g_animationCache.clear();
}

void closeAnimFile()
{
    if (g_animFile) {
        g_animFile.close();
    }
    g_animFilePath = "";
}

bool readAnimContainerFile(const String& path, const String& label, DynamicAnimation& animation)
{
    File file = SharedSd::fs().open(path.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return false;
    }

    uint8_t header[kAnimHeaderBytes] = {};
    if (file.read(header, sizeof(header)) != sizeof(header)) {
        file.close();
        return false;
    }
    if (memcmp(header, kAnimMagic, sizeof(kAnimMagic)) != 0) {
        file.close();
        return false;
    }

    const uint16_t headerBytes = readLe16(header + 8);
    const uint16_t flags = readLe16(header + 10);
    const uint16_t canvasWidth = readLe16(header + 12);
    const uint16_t canvasHeight = readLe16(header + 14);
    const uint16_t frameCount = readLe16(header + 18);
    const uint32_t tableOffset = readLe32(header + 20);
    if (headerBytes < kAnimHeaderBytes || (flags & 0x0001U) == 0
        || canvasWidth == 0 || canvasHeight == 0 || frameCount == 0
        || frameCount > 2000 || tableOffset < headerBytes) {
        file.close();
        return false;
    }

    if (!file.seek(tableOffset)) {
        file.close();
        return false;
    }

    std::vector<DynamicAnimation::Frame> frames;
    frames.reserve(frameCount);
    size_t maxPixels = 0;
    const uint32_t fileSize = static_cast<uint32_t>(file.size());
    for (uint16_t i = 0; i < frameCount; ++i) {
        uint8_t entry[kAnimFrameEntryBytes] = {};
        if (file.read(entry, sizeof(entry)) != sizeof(entry)) {
            frames.clear();
            break;
        }

        DynamicAnimation::Frame frame;
        frame.dataOffset = readLe32(entry + 0);
        frame.dataSize = readLe32(entry + 4);
        frame.width = readLe16(entry + 8);
        frame.height = readLe16(entry + 10);
        frame.offsetX = readLe16(entry + 12);
        frame.offsetY = readLe16(entry + 14);
        const uint16_t delayMs = readLe16(entry + 16);
        const uint16_t frameFlags = readLe16(entry + 18);
        (void)delayMs;
        frame.canvasWidth = canvasWidth;
        frame.canvasHeight = canvasHeight;
        if ((frameFlags & kAnimFrameFlagScale4x) != 0 || (frame.width == 120 && frame.height == 120)) {
            frame.scale = 4;
        } else if ((frameFlags & kAnimFrameFlagScale2x) != 0 || (frame.width == 240 && frame.height == 240)) {
            frame.scale = 2;
        } else {
            frame.scale = 1;
        }
        const size_t pixels = static_cast<size_t>(frame.width) * frame.height;
        if (frame.width == 0 || frame.height == 0 || pixels > kPersistentFrameBufferPixels
            || frame.dataSize == 0 || frame.dataOffset >= fileSize
            || frame.dataOffset + frame.dataSize > fileSize) {
            frames.clear();
            break;
        }
        maxPixels = std::max(maxPixels, pixels);
        frames.push_back(frame);
    }
    file.close();

    if (frames.empty() || maxPixels > kPersistentFrameBufferPixels) {
        return false;
    }

    animation.animPath = path;
    animation.label = label.length() > 0 ? label : baseNameWithoutExt(path);
    animation.frames = std::move(frames);
    animation.frameCount = animation.frames.size();
    animation.width = animation.frames[0].width;
    animation.height = animation.frames[0].height;
    animation.offsetX = animation.frames[0].offsetX;
    animation.offsetY = animation.frames[0].offsetY;
    animation.canvasWidth = canvasWidth;
    animation.canvasHeight = canvasHeight;
    animation.scale = animation.frames[0].scale;
    animation.animContainer = true;
    return true;
}

String childPath(const String& parent, const char* name)
{
    String value(name != nullptr ? name : "");
    if (value.startsWith("/")) {
        return value;
    }
    return parent + "/" + value;
}

bool probeDynamicAnimFile(const String& path, const String& label, DynamicAnimation& out)
{
    if (!isAnimFileName(path)) {
        return false;
    }
    return readAnimContainerFile(path, label, out);
}

void appendCachedEntry(CachedAnimationCategory& cache, const String& path, const String& label)
{
    if (path.isEmpty()) {
        return;
    }
    for (const auto& entry : cache.entries) {
        if (entry.path == path) {
            return;
        }
    }

    CachedAnimationEntry entry;
    entry.path = path;
    entry.label = label.length() > 0 ? label : animationLabelFromPath(path);
    cache.entries.push_back(std::move(entry));
}

CachedAnimationCategory* categoryCacheForPath(const String& categoryPath)
{
    for (auto& cache : g_animationCache) {
        if (cache.path == categoryPath) {
            return &cache;
        }
    }

    CachedAnimationCategory cache;
    cache.path = categoryPath;
    g_animationCache.push_back(std::move(cache));
    return &g_animationCache.back();
}

bool scanCategorySequences(CachedAnimationCategory& cache)
{
    if (cache.scanned) {
        return !cache.entries.empty();
    }

    File root = SharedSd::fs().open(cache.path.c_str(), FILE_READ);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        return false;
    }
    cache.scanned = true;

    while (true) {
        File entry = root.openNextFile();
        if (!entry) {
            break;
        }
        if (entry.isDirectory()) {
            const String dir = childPath(cache.path, entry.name());
            const String animPath = dir + "/animation.anim";
            File anim = SharedSd::fs().open(animPath.c_str(), FILE_READ);
            if (anim && !anim.isDirectory()) {
                appendCachedEntry(cache, animPath, animationLabelFromPath(animPath));
            }
            if (anim) {
                anim.close();
            }
        } else {
            const String path = childPath(cache.path, entry.name());
            if (isAnimFileName(path)) {
                appendCachedEntry(cache, path, animationLabelFromPath(path));
            }
        }
        entry.close();
    }
    root.close();

    return !cache.entries.empty();
}

bool selectDynamicSequenceFromPath(const String& categoryPath,
    DynamicPickMode pickMode, size_t& sequentialIndex, DynamicAnimation& out)
{
    CachedAnimationCategory* cache = categoryCacheForPath(categoryPath);
    if (cache == nullptr || !scanCategorySequences(*cache)) {
        return false;
    }

    const size_t matchCount = cache->entries.size();
    if (matchCount == 0) {
        return false;
    }

    size_t startIndex = 0;
    if (pickMode == DynamicPickMode::Sequential) {
        startIndex = sequentialIndex % matchCount;
    } else {
        startIndex = randomModulo(static_cast<uint32_t>(matchCount));
        if (matchCount > 1 && startIndex == cache->lastRandomIndex) {
            startIndex = (startIndex + 1 + randomModulo(static_cast<uint32_t>(matchCount - 1))) % matchCount;
        }
    }

    for (size_t attempt = 0; attempt < matchCount; ++attempt) {
        const size_t index = (startIndex + attempt) % matchCount;
        const CachedAnimationEntry& entry = cache->entries[index];
        if (probeDynamicAnimFile(entry.path, entry.label, out)) {
            out.categoryPath = categoryPath;
            out.categoryEntryCount = matchCount;
            cache->lastRandomIndex = index;
            if (pickMode == DynamicPickMode::Sequential) {
                sequentialIndex = (index + 1) % matchCount;
            }
            return true;
        }
    }

    return false;
}

bool selectDynamicSequence(const char* const* categories, size_t categoryCount,
    DynamicPickMode pickMode, size_t& sequentialIndex, DynamicAnimation& out)
{
    if (!SharedSd::isMounted()) {
        return false;
    }
    for (size_t i = 0; i < categoryCount; ++i) {
        const char* category = categories[i];
        if (category == nullptr || category[0] == '\0') {
            continue;
        }
        if (selectDynamicSequenceFromPath(String("/faces/") + category,
                pickMode, sequentialIndex, out)) {
            return true;
        }
    }
    return false;
}

bool moodUsesIdleBase(ImageSource::Mood mood)
{
    return mood == ImageSource::Mood::Neutral
        || mood == ImageSource::Mood::Paused
        || mood == ImageSource::Mood::Angry;
}

bool moodAllowsSpecial(ImageSource::Mood mood)
{
    return mood == ImageSource::Mood::Neutral
        || mood == ImageSource::Mood::Paused
        || mood == ImageSource::Mood::Break;
}

void scheduleNextSpecial(uint32_t nowMs)
{
    g_nextSpecialMs = nowMs + kSpecialMinIntervalMs + randomModulo(kSpecialJitterMs);
}

void startDynamicPlayback(const DynamicAnimation& animation, bool oneShot)
{
    closeAnimFile();
    g_dynamicActive = animation;
    g_activeDynamic = true;
    g_oneShotActive = oneShot;
    g_loadedFrameIndex = static_cast<size_t>(-1);
    g_nextFrameMs = 0;
    g_baseLoopCount = 0;
    g_errorText = "";
}

bool startDynamicCategory(const char* const* categories, size_t categoryCount,
    bool oneShot, DynamicPickMode pickMode = DynamicPickMode::Random)
{
    static size_t randomSequenceIndex = 0;
    size_t& sequenceIndex = pickMode == DynamicPickMode::Sequential
        ? g_dynamicSpecialIndex
        : randomSequenceIndex;
    DynamicAnimation animation;
    if (!selectDynamicSequence(categories, categoryCount, pickMode, sequenceIndex, animation)) {
        return false;
    }
    startDynamicPlayback(animation, oneShot);
    return true;
}

bool startDynamicBaseForMood(ImageSource::Mood mood)
{
    switch (mood) {
        case ImageSource::Mood::Focus: {
            constexpr const char* categories[] = {"focus", "foco"};
            return startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false);
        }
        case ImageSource::Mood::Break: {
            constexpr const char* categories[] = {"break", "descanso", "normal"};
            return startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false);
        }
        case ImageSource::Mood::Sleep: {
            constexpr const char* categories[] = {"sleep", "dormir", "sleepy"};
            if (startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false)) {
                return true;
            }
            DynamicAnimation animation;
            if (readAnimContainerFile("/faces/normal/sleepy_2.anim", "sleep", animation)) {
                startDynamicPlayback(animation, false);
                return true;
            }
            constexpr const char* fallbackCategories[] = {"normal", "idle"};
            return startDynamicCategory(fallbackCategories,
                sizeof(fallbackCategories) / sizeof(fallbackCategories[0]), false);
        }
        case ImageSource::Mood::Hydrate: {
            constexpr const char* categories[] = {"agua", "water"};
            return startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false);
        }
        case ImageSource::Mood::Stretch: {
            constexpr const char* categories[] = {"ojos", "eyes", "vista", "stretch"};
            return startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false);
        }
        case ImageSource::Mood::Paused: {
            constexpr const char* categories[] = {"normal", "idle"};
            return startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false);
        }
        case ImageSource::Mood::Angry:
        case ImageSource::Mood::Neutral:
        default: {
            constexpr const char* categories[] = {"normal", "idle"};
            return startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), false);
        }
    }
}

void applyRequestedAnimation(bool force = false)
{
    if (force || !g_oneShotActive) {
        if (startDynamicBaseForMood(g_requestedMood)) {
            return;
        }
        closeAnimFile();
        g_activeDynamic = false;
        g_loadedFrameIndex = static_cast<size_t>(-1);
        g_errorText = "NO .ANIM";
    }
}

void startRandomSpecial(uint32_t nowMs)
{
    if (!moodAllowsSpecial(g_requestedMood) || g_oneShotActive) {
        scheduleNextSpecial(nowMs);
        return;
    }
    constexpr const char* dynamicSpecialCategories[] = {"especial", "special"};
    if (startDynamicCategory(dynamicSpecialCategories,
            sizeof(dynamicSpecialCategories) / sizeof(dynamicSpecialCategories[0]),
            true, DynamicPickMode::Sequential)) {
        scheduleNextSpecial(nowMs);
        return;
    }
    scheduleNextSpecial(nowMs);
}

bool loadActiveAnimation()
{
    applyRequestedAnimation();
    g_demoLoaded = true;
    g_loadedFrameIndex = static_cast<size_t>(-1);
    g_errorText = "";
    g_nextFrameMs = 0;
    if (!loadFrame(0)) {
        g_demoLoaded = false;
        g_loadedFrameIndex = static_cast<size_t>(-1);
        return false;
    }
    return true;
}

bool ensureActiveAnimFile()
{
    if (g_dynamicActive.animPath.isEmpty()) {
        return false;
    }
    if (g_animFile && g_animFilePath == g_dynamicActive.animPath) {
        return true;
    }
    closeAnimFile();
    g_animFile = SharedSd::fs().open(g_dynamicActive.animPath.c_str(), FILE_READ);
    if (!g_animFile) {
        g_errorText = String("NO ") + g_dynamicActive.animPath;
        return false;
    }
    g_animFilePath = g_dynamicActive.animPath;
    return true;
}

bool loadAnimRleFrame(const DynamicAnimation::Frame& frame)
{
    const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    if (!ensureFrameBuffer(pixelCount) || !ensureActiveAnimFile()) {
        return false;
    }
    if (!g_animFile.seek(frame.dataOffset)) {
        g_errorText = "SEEK ANIM";
        return false;
    }

    uint16_t* out = g_frameBuffer;
    size_t outPixels = 0;
    uint32_t remaining = frame.dataSize;
    size_t lastYieldPixels = 0;
    static uint8_t rleChunk[kReadChunkBytes];
    if ((remaining % 4U) != 0) {
        g_errorText = "BAD RLE SIZE";
        return false;
    }

    while (remaining > 0 && outPixels < pixelCount) {
        size_t toRead = remaining > kReadChunkBytes ? kReadChunkBytes : remaining;
        toRead -= (toRead % 4U);
        if (toRead == 0) {
            g_errorText = "BAD RLE CHUNK";
            return false;
        }

        if (g_animFile.read(rleChunk, toRead) != toRead) {
            g_errorText = "SHORT ANIM";
            return false;
        }

        remaining -= static_cast<uint32_t>(toRead);
        for (size_t pos = 0; pos < toRead; pos += 4) {
            const uint16_t count = readLe16(rleChunk + pos);
            const uint16_t color = readLe16(rleChunk + pos + 2);
            if (count == 0 || outPixels + count > pixelCount) {
                g_errorText = "BAD RLE";
                return false;
            }
            for (uint16_t i = 0; i < count; ++i) {
                out[outPixels++] = color;
            }
            if ((outPixels - lastYieldPixels) >= 16384U) {
                lastYieldPixels = outPixels;
                yield();
            }
        }
    }
    if (remaining != 0 || outPixels != pixelCount) {
        g_errorText = "BAD ANIM";
        return false;
    }
    return true;
}

bool loadFrame(size_t frameIndex)
{
    if (g_activeDynamic) {
        if (!g_demoLoaded || frameIndex >= g_dynamicActive.frameCount) {
            return false;
        }
        uint16_t frameWidth = g_dynamicActive.width;
        uint16_t frameHeight = g_dynamicActive.height;
        uint16_t frameOffsetX = g_dynamicActive.offsetX;
        uint16_t frameOffsetY = g_dynamicActive.offsetY;
        uint16_t frameCanvasWidth = g_dynamicActive.canvasWidth;
        uint16_t frameCanvasHeight = g_dynamicActive.canvasHeight;
        uint8_t frameScale = g_dynamicActive.scale;
        if (g_dynamicActive.frames.empty() || !g_dynamicActive.animContainer) {
            g_errorText = "NO .ANIM";
            return false;
        }
        const DynamicAnimation::Frame& frame = g_dynamicActive.frames[frameIndex];
        frameWidth = frame.width;
        frameHeight = frame.height;
        frameOffsetX = frame.offsetX;
        frameOffsetY = frame.offsetY;
        frameCanvasWidth = frame.canvasWidth;
        frameCanvasHeight = frame.canvasHeight;
        frameScale = frame.scale;
        if (!loadAnimRleFrame(frame)) {
            return false;
        }

        g_loadedFrameIndex = frameIndex;
        g_dynamicActive.width = frameWidth;
        g_dynamicActive.height = frameHeight;
        g_dynamicActive.offsetX = frameOffsetX;
        g_dynamicActive.offsetY = frameOffsetY;
        g_dynamicActive.canvasWidth = frameCanvasWidth;
        g_dynamicActive.canvasHeight = frameCanvasHeight;
        g_dynamicActive.scale = frameScale;
        snprintf(g_detail, sizeof(g_detail), "SD %s %u/%u %ux%u",
            g_dynamicActive.label.c_str(),
            static_cast<unsigned>(frameIndex + 1),
            static_cast<unsigned>(g_dynamicActive.frameCount),
            static_cast<unsigned>(frameWidth),
            static_cast<unsigned>(frameHeight));
        g_errorText = "";
        return true;
    }

    return false;
}

bool recoverOneShotToBaseFrame()
{
    if (!g_oneShotActive) {
        return false;
    }

    g_oneShotActive = false;
    applyRequestedAnimation(true);
    return loadFrame(0);
}

bool advancePlaybackFrame(uint32_t nowMs)
{
    const size_t frameCount = g_activeDynamic ? g_dynamicActive.frameCount : 0;
    if (frameCount == 0) {
        return false;
    }

    if ((g_loadedFrameIndex + 1) < frameCount) {
        if (loadFrame(g_loadedFrameIndex + 1)) {
            return true;
        }
        return recoverOneShotToBaseFrame();
    }

    ++g_baseLoopCount;

    if (g_oneShotActive) {
        if (g_baseLoopCount < kLoopsBeforeChange) {
            return loadFrame(0);
        }
        g_oneShotActive = false;
        applyRequestedAnimation(true);
        return loadFrame(0);
    }

    if (g_baseLoopCount < kLoopsBeforeChange) {
        return loadFrame(0);
    }

    if (g_nextSpecialMs == 0) {
        scheduleNextSpecial(nowMs);
    } else if (static_cast<int32_t>(nowMs - g_nextSpecialMs) >= 0) {
        startRandomSpecial(nowMs);
        return loadFrame(0);
    }

    if (g_dynamicActive.categoryEntryCount > 1
        && startDynamicBaseForMood(g_requestedMood)) {
        return loadFrame(0);
    }

    return loadFrame(0);
}

ImageFrame565View fallbackFrame()
{
    ImageFrame565View view;
    view.title = "SD IMAGE";
    view.detail = g_errorText.isEmpty() ? "NO DATA" : g_errorText.c_str();
    return view;
}
}

namespace ImageSource
{
void reserveMemory()
{
    ensureFrameBuffer(1);
}

bool begin(Board& board)
{
    g_board = &board;
    if (SharedSd::isMounted()) {
        if (g_demoLoaded) {
            return true;
        }
        if (!loadActiveAnimation()) {
            RADAR_LOG_PRINTLN("WARN: image source animation not available");
            return failBegin(g_errorText.c_str());
        }
        g_nextSdRetryMs = 0;
        return true;
    }

    if (!SharedSd::begin(board)) {
        RADAR_LOG_PRINTLN("WARN: image source could not mount SD");
        return failBegin("SD OFF");
    }

    if (!loadActiveAnimation()) {
        RADAR_LOG_PRINTLN("WARN: image source animation not available");
        return failBegin(g_errorText.c_str());
    }

    g_nextSdRetryMs = 0;
    return true;
}

void suspend()
{
    closeAnimFile();
    g_loadedFrameIndex = static_cast<size_t>(-1);
    g_demoLoaded = false;
    g_nextFrameMs = 0;
    g_oneShotActive = false;
}

void end()
{
    suspend();
    clearAnimationCache();
}

void setMood(Mood mood)
{
    if (g_requestedMood == mood) {
        return;
    }
    g_requestedMood = mood;
    const bool highPriority = mood == Mood::Focus || mood == Mood::Break
        || mood == Mood::Hydrate || mood == Mood::Stretch;
    applyRequestedAnimation(highPriority);
}

void playMoment(Moment moment, uint32_t nowMs)
{
    switch (moment) {
        case Moment::PomodoroPaused: {
            constexpr const char* categories[] = {"stop", "pause", "paused"};
            scheduleNextSpecial(nowMs);
            startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), true);
            break;
        }
        case Moment::PomodoroReset: {
            constexpr const char* categories[] = {"llorar", "cry", "crying", "reset"};
            scheduleNextSpecial(nowMs);
            startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), true);
            break;
        }
        case Moment::PomodoroCompleted: {
            constexpr const char* categories[] = {"celebrar", "celebrate", "happy", "completo"};
            scheduleNextSpecial(nowMs);
            startDynamicCategory(categories, sizeof(categories) / sizeof(categories[0]), true);
            break;
        }
    }
}

void notifyInteraction(Interaction interaction, uint32_t nowMs)
{
    (void)interaction;
    (void)nowMs;
}

void setFrameIntervalMs(uint32_t intervalMs)
{
    g_frameIntervalMs = std::max<uint32_t>(1, intervalMs);
}

ImageFrame565View currentImageFrame(uint32_t nowMs)
{
    if ((!SharedSd::isMounted() || !g_demoLoaded)
        && g_board != nullptr
        && (g_nextSdRetryMs == 0 || nowMs >= g_nextSdRetryMs)) {
        begin(*g_board);
    }

    if (!SharedSd::isMounted() || !g_demoLoaded) {
        return fallbackFrame();
    }

    if (g_loadedFrameIndex == static_cast<size_t>(-1)) {
        if (!loadFrame(0)) {
            if (recoverOneShotToBaseFrame()) {
                g_nextFrameMs = nowMs + g_frameIntervalMs;
            } else {
                return fallbackFrame();
            }
        } else {
            g_nextFrameMs = nowMs + g_frameIntervalMs;
        }
    } else if (g_nextFrameMs == 0) {
        g_nextFrameMs = nowMs + g_frameIntervalMs;
    } else if (nowMs >= g_nextFrameMs) {
        if (!g_oneShotActive && moodAllowsSpecial(g_requestedMood)) {
            if (g_nextSpecialMs == 0) {
                scheduleNextSpecial(nowMs);
            } else if (static_cast<int32_t>(nowMs - g_nextSpecialMs) >= 0) {
                startRandomSpecial(nowMs);
            }
        }
        if (advancePlaybackFrame(nowMs)) {
            g_nextFrameMs = nowMs + g_frameIntervalMs;
        } else {
            g_nextFrameMs = nowMs + (g_frameIntervalMs * 2UL);
        }
    }

    if (g_loadedFrameIndex == static_cast<size_t>(-1)) {
        return fallbackFrame();
    }

    ImageFrame565View view;
    view.pixels = g_frameBuffer;
    if (g_activeDynamic) {
        view.width = g_dynamicActive.width;
        view.height = g_dynamicActive.height;
        view.canvasWidth = g_dynamicActive.canvasWidth;
        view.canvasHeight = g_dynamicActive.canvasHeight;
        view.offsetX = g_dynamicActive.offsetX;
        view.offsetY = g_dynamicActive.offsetY;
        view.scale = g_dynamicActive.scale;
        view.title = "FACE";
        view.detail = g_detail;
        return view;
    }

    return view;
}
}
