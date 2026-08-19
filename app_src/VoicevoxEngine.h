#pragma once
#include <filesystem>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <array>
#include <mmsystem.h>
#include "voicevox_core.h"

class VoicevoxEngine {
public:
    ~VoicevoxEngine();
    bool Initialize(const std::filesystem::path& directory);
    bool Speak(const std::wstring& text);
    void SpeakLines(const std::vector<std::wstring>& lines, const std::wstring& threadId = L"");
    void Cancel();
    void SetVoiceSettings(double speed, DWORD endSilenceMs);
    void SetSpeaker(uint32_t speakerId);
    void SetSpatialPosition(int centerX, int screenWidth);
    void ClearSpatialPosition();
    bool SetVoiceModel(const std::filesystem::path& path);
    void SetLipSyncCallback(std::function<void(wchar_t)> callback);
    std::string Error() const { std::lock_guard<std::mutex> lock(errorMutex_); return error_; }
private:
    struct AudioItem {
        uint8_t* wav = nullptr;
        uintptr_t length = 0;
        uint64_t requestId = 0;
        std::vector<std::pair<wchar_t, double>> mouthSchedule;
        int leftDelayMs = 0;
        int rightDelayMs = 0;
        double leftGain = 1.0;
        double rightGain = 1.0;
        std::wstring threadId;
        size_t playbackSlot = 0;
    };
    struct TextItem {
        std::wstring text;
        std::wstring threadId;
        uint64_t requestId;
        std::filesystem::path modelPath;
        uint32_t speakerId;
        int leftDelayMs;
        int rightDelayMs;
        double leftGain;
        double rightGain;
    };
    void SynthesisLoop();
    void PlaybackLoop(size_t playbackSlot);
    void ClearAudioQueue();
    void NotifyLipSync(wchar_t mouth);
    std::string error_;
    mutable std::mutex errorMutex_;
    const VoicevoxOnnxruntime* runtime_ = nullptr;
    OpenJtalkRc* jtalk_ = nullptr;
    VoicevoxSynthesizer* synthesizer_ = nullptr;
    VoicevoxVoiceModelFile* model_ = nullptr;
    std::filesystem::path modelPath_;
    std::filesystem::path iniPath_;
    std::atomic<double> speed_{1.0};
    std::atomic<DWORD> endSilenceMs_{300};
    std::atomic<uint32_t> speakerId_{23};
    std::atomic<int> spatialLeftDelayMs_{0};
    std::atomic<int> spatialRightDelayMs_{0};
    std::atomic<double> spatialLeftGain_{1.0};
    std::atomic<double> spatialRightGain_{1.0};
    std::vector<std::thread> playbackThreads_;
    std::thread synthesisThread_;
    std::atomic_uint64_t requestId_{0};
    std::mutex queueMutex_;
    std::mutex modelMutex_;
    std::condition_variable queueCondition_;
    std::deque<TextItem> textQueue_;
    std::wstring lastSynthesizedThreadId_;
    std::unordered_map<std::wstring, uint64_t> threadRequests_;
    std::array<std::wstring, 3> playbackSlotOwners_;
    std::unordered_map<std::wstring, size_t> threadSlots_;
    std::mutex playbackMutexMapMutex_;
    std::unordered_map<std::wstring, std::shared_ptr<std::mutex>> threadPlaybackMutexes_;
    std::mutex audioMutex_;
    std::condition_variable audioCondition_;
    std::array<std::deque<AudioItem>, 3> audioQueues_;
    std::mutex lipSyncMutex_;
    std::function<void(wchar_t)> lipSyncCallback_;
    std::atomic_bool stopping_{ false };
    uint64_t RequestFor(const std::wstring& threadId);
};
