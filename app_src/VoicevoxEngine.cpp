#include "pch.h"
#include "VoicevoxEngine.h"
#include <mmsystem.h>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <regex>
#include <chrono>
#include <cmath>
#include <shlobj.h>
#pragma comment(lib, "winmm.lib")

// 音声出力に対するリップシンクの遅延補正値（ミリ秒）。
constexpr DWORD kLipSyncDelayMs = 350;

static double ReadSpeed(const std::filesystem::path& ini) {
    wchar_t value[64] = L"1.0";
    GetPrivateProfileStringW(L"Voice", L"Speed", L"1.0", value, 64, ini.c_str());
    const double speed = _wtof(value);
    return speed > 0.1 && speed < 10.0 ? speed : 1.0;
}
static DWORD ReadEndSilenceMs(const std::filesystem::path& ini) {
    const DWORD value = GetPrivateProfileIntW(L"Voice", L"EndSilenceMs", 300, ini.c_str());
    return value <= 60000 ? value : 300;
}
static void ReplaceSpeed(std::string& json, double speed) {
    const std::string marker = "\"speedScale\":";
    const size_t begin = json.find(marker);
    if (begin == std::string::npos) return;
    const size_t first = begin + marker.size();
    size_t last = first;
    while (last < json.size() && (std::isdigit(static_cast<unsigned char>(json[last])) || json[last] == '.' || json[last] == '-')) ++last;
    json.replace(first, last - first, std::to_string(speed));
}
static void ReplacePostPhonemeLength(std::string& json, double seconds) {
    const std::string marker = "\"postPhonemeLength\":";
    const size_t begin = json.find(marker);
    if (begin == std::string::npos) return;
    const size_t first = begin + marker.size();
    size_t last = first;
    while (last < json.size() && (std::isdigit(static_cast<unsigned char>(json[last])) || json[last] == '.')) ++last;
    json.replace(first, last - first, std::to_string(seconds));
}

static std::string Utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string r(static_cast<size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, r.data(), n, nullptr, nullptr) <= 0) return {};
    r.resize(static_cast<size_t>(n - 1));
    return r;
}
static bool Ok(VoicevoxResultCode r, const char* op, std::string& e, std::mutex& errorMutex) {
    if (r == VOICEVOX_RESULT_OK) return true;
    std::lock_guard<std::mutex> lock(errorMutex);
    e = std::string(op) + ": " + voicevox_error_result_to_message(r);
    return false;
}
void VoicevoxEngine::SetLipSyncCallback(std::function<void(wchar_t)> callback) {
    std::lock_guard<std::mutex> lock(lipSyncMutex_);
    lipSyncCallback_ = std::move(callback);
}
void VoicevoxEngine::NotifyLipSync(wchar_t mouth) {
    std::function<void(wchar_t)> callback;
    { std::lock_guard<std::mutex> lock(lipSyncMutex_); callback = lipSyncCallback_; }
    if (callback) callback(mouth);
}
VoicevoxEngine::~VoicevoxEngine() {
    requestId_++;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopping_ = true;
        textQueue_.clear();
    }
    ClearAudioQueue();
    Cancel();
    queueCondition_.notify_all();
    audioCondition_.notify_all();
    if (synthesisThread_.joinable()) synthesisThread_.join();
    for (auto& thread : playbackThreads_) if (thread.joinable()) thread.join();
    if (model_) voicevox_voice_model_file_delete(model_);
    if (synthesizer_) voicevox_synthesizer_delete(synthesizer_);
    if (jtalk_) voicevox_open_jtalk_rc_delete(jtalk_);
}
bool VoicevoxEngine::Initialize(const std::filesystem::path& dir) {
    wchar_t localAppData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        iniPath_ = std::filesystem::path(localAppData) / L"CodexParakeet" / L"CodexParakeet.ini";
        std::error_code error;
        std::filesystem::create_directories(iniPath_.parent_path(), error);
    } else {
        iniPath_ = dir / L"CodexParakeet.ini";
    }
    const auto root = dir / L"voicevox_core";
    const auto ort = (root / L"onnxruntime/lib/voicevox_onnxruntime.dll").string();
    const auto dic = (root / L"dict/open_jtalk_dic_utf_8-1.11").string();
    const auto model = (root / L"models/vvms/8.vvm").string();
    auto options = voicevox_make_default_load_onnxruntime_options();
    options.filename = ort.c_str();
    if (!Ok(voicevox_onnxruntime_load_once(options, &runtime_), "ONNX Runtime", error_, errorMutex_)) return false;
    if (!Ok(voicevox_open_jtalk_rc_new(dic.c_str(), &jtalk_), "Open JTalk", error_, errorMutex_)) return false;
    if (!Ok(voicevox_synthesizer_new(runtime_, jtalk_, voicevox_make_default_initialize_options(), &synthesizer_), "synthesizer", error_, errorMutex_)) return false;
    if (!Ok(voicevox_voice_model_file_open(model.c_str(), &model_), "WhiteCUL model", error_, errorMutex_)) return false;

	VoicevoxLoadVoiceModelOptions load_voice_options{};
	load_voice_options.on_existing = VOICEVOX_ON_EXISTING_VOICE_MODEL_ID_SKIP;
    if (!Ok(voicevox_synthesizer_load_voice_model(synthesizer_, model_, load_voice_options), "load WhiteCUL model", error_, errorMutex_)) return false;

    modelPath_ = model;
    synthesisThread_ = std::thread(&VoicevoxEngine::SynthesisLoop, this);
    // 音声項目ごとに別ワーカーが waveOut を開けるよう、再生ワーカーを複数用意する。
    // 同一スレッドIDのキャンセルは世代番号で止め、別IDの音声は別ワーカーで重畳する。
    constexpr int playbackWorkerCount = 3;
    for (int i = 0; i < playbackWorkerCount; ++i)
        playbackThreads_.emplace_back(&VoicevoxEngine::PlaybackLoop, this, static_cast<size_t>(i));
    return true;
}
bool VoicevoxEngine::Speak(const std::wstring& text) {
    SpeakLines({ text }, L"");
    return true;
}

uint64_t VoicevoxEngine::RequestFor(const std::wstring& threadId) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return threadRequests_[threadId];
}

void VoicevoxEngine::SpeakLines(const std::vector<std::wstring>& lines, const std::wstring& threadId) {
    const uint64_t requestId = ++requestId_;
    std::filesystem::path requestModel;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        requestModel = modelPath_;
    }
    const uint32_t requestSpeaker = speakerId_.load();
    const int requestLeftDelayMs = spatialLeftDelayMs_.load();
    const int requestRightDelayMs = spatialRightDelayMs_.load();
    const double requestLeftGain = spatialLeftGain_.load();
    const double requestRightGain = spatialRightGain_.load();
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        // 同時に保持するスレッドIDは3つまで。4つ目以降は最初の枠を奪う。
        if (threadSlots_.find(threadId) == threadSlots_.end()) {
            size_t slot = 0;
            while (slot < playbackSlotOwners_.size() &&
                std::find_if(threadSlots_.begin(), threadSlots_.end(),
                    [slot](const auto& entry) { return entry.second == slot; }) != threadSlots_.end()) ++slot;
            if (slot == playbackSlotOwners_.size()) {
                const std::wstring evicted = playbackSlotOwners_[0];
                threadRequests_[evicted] = ++requestId_;
                threadSlots_.erase(evicted);
                slot = 0;
            }
            playbackSlotOwners_[slot] = threadId;
            threadSlots_[threadId] = slot;
        }
        threadRequests_[threadId] = requestId;
        for (auto it = textQueue_.begin(); it != textQueue_.end();) {
            if (it->threadId == threadId) it = textQueue_.erase(it); else ++it;
        }
        for (const auto& line : lines) if (!line.empty())
            textQueue_.push_back({ line, threadId, requestId, requestModel, requestSpeaker,
                requestLeftDelayMs, requestRightDelayMs, requestLeftGain, requestRightGain });
    }
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        for (auto& queue : audioQueues_) {
            for (auto it = queue.begin(); it != queue.end();) {
                if (it->threadId == threadId) { voicevox_wav_free(it->wav); it = queue.erase(it); } else ++it;
            }
        }
    }
    queueCondition_.notify_all();
    audioCondition_.notify_all();
}

void VoicevoxEngine::SynthesisLoop() {
    for (;;) {
        TextItem job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this] { return stopping_ || !textQueue_.empty(); });
            if (stopping_) return;
            // 直前と同じターミナルを連続処理せず、別ターミナルの行を優先する。
            // 同じターミナルしか待っていない場合は、そのまま先頭を処理する。
            auto next = textQueue_.begin();
            if (!lastSynthesizedThreadId_.empty()) {
                const auto different = std::find_if(textQueue_.begin(), textQueue_.end(),
                    [this](const TextItem& item) { return item.threadId != lastSynthesizedThreadId_; });
                if (different != textQueue_.end()) next = different;
            }
            job = std::move(*next);
            textQueue_.erase(next);
            lastSynthesizedThreadId_ = job.threadId;
        }
        const auto& text = job.text;
        // 待機中に到着した最新リクエスト番号を、取り出した直後に取得する。
        const uint64_t requestId = job.requestId;
        uintptr_t length = 0; uint8_t* wav = nullptr;
        char* query = nullptr;
        std::lock_guard<std::mutex> modelLock(modelMutex_);
        if (modelPath_ != job.modelPath) {
            VoicevoxVoiceModelFile* nextModel = nullptr;
            const auto modelUtf8 = job.modelPath.string();

            VoicevoxLoadVoiceModelOptions load_voice_options{};
            load_voice_options.on_existing = VOICEVOX_ON_EXISTING_VOICE_MODEL_ID_SKIP;

            if (!Ok(voicevox_voice_model_file_open(modelUtf8.c_str(), &nextModel), "open queued voice model", error_, errorMutex_) ||
                !Ok(voicevox_synthesizer_load_voice_model(synthesizer_, nextModel, load_voice_options), "load queued voice model", error_, errorMutex_)) {
                if (nextModel) voicevox_voice_model_file_delete(nextModel);
                continue;
            }
            if (model_) voicevox_voice_model_file_delete(model_);
            model_ = nextModel;
            modelPath_ = job.modelPath;
        }
        const auto utf8 = Utf8(text);
        const uint32_t speakerId = job.speakerId;
        if (!Ok(voicevox_synthesizer_create_audio_query(synthesizer_, utf8.c_str(), speakerId, &query), "audio query", error_, errorMutex_)) {
            continue;
        }
        std::string queryText(query);
        voicevox_json_free(query);
        ReplaceSpeed(queryText, speed_.load());
        ReplacePostPhonemeLength(queryText, 0.5);
        if (!Ok(voicevox_synthesizer_synthesis(synthesizer_, queryText.c_str(), speakerId,
            voicevox_make_default_synthesis_options(), &length, &wav), "synthesis", error_, errorMutex_)) {
            continue;
        }
        if (RequestFor(job.threadId) != requestId) {
            voicevox_wav_free(wav);
            continue;
        }
        // audio_query のモーラ情報から、再生中に表示する母音の予定を作る。
        // 子音がないモーラでは consonant_length が null になるため、
        // nullまたは数値の両方を受け付ける。
        std::vector<std::pair<wchar_t, double>> mouthSchedule;
        double prePhonemeLength = 0.0;
        const std::regex prePhonemeRegex(R"REGEX("prePhonemeLength"\s*:\s*([0-9.eE+-]+))REGEX");
        std::smatch prePhonemeMatch;
        if (std::regex_search(queryText, prePhonemeMatch, prePhonemeRegex)) {
            prePhonemeLength = std::stod(prePhonemeMatch.str(1));
        }
        const std::regex moraRegex(R"REGEX("consonant_length"\s*:\s*(null|[0-9.eE+-]+)\s*,\s*"vowel"\s*:\s*"([^"]+)"\s*,\s*"vowel_length"\s*:\s*([0-9.eE+-]+))REGEX");
        for (std::sregex_iterator i(queryText.begin(), queryText.end(), moraRegex), end; i != end; ++i) {
            const auto& m = *i;
            const std::string vowelText = m.str(2);
            const wchar_t vowel = vowelText.size() == 1 &&
                std::string("aiueo").find(vowelText[0]) != std::string::npos
                ? static_cast<wchar_t>(vowelText[0]) : L'c';
            const double consonantLength = m.str(1) == "null" ? 0.0 : std::stod(m.str(1));
            const double vowelLength = std::stod(m.str(3));
            if (consonantLength > 0.0) mouthSchedule.emplace_back(L'c', consonantLength);
            mouthSchedule.emplace_back(vowel, vowelLength);
        }
        // speedScale は音声の再生時間を変えるため、口形側の時間も同じ倍率で補正する。
        const double speechSpeed = speed_.load();
        for (auto& [mouth, duration] : mouthSchedule) {
            UNREFERENCED_PARAMETER(mouth);
            duration /= speechSpeed;
        }
        // 発声前の無音は口閉じとして保持する。音声出力との全体遅延は
        // PlaybackLoop側のkLipSyncDelayMsで補正する。
        mouthSchedule.insert(mouthSchedule.begin(), { L'c', prePhonemeLength / speechSpeed });
        size_t playbackSlot = 0;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            const auto slot = threadSlots_.find(job.threadId);
            if (slot == threadSlots_.end()) {
                voicevox_wav_free(wav);
                continue;
            }
            playbackSlot = slot->second;
        }
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioQueues_[playbackSlot].push_back({ wav, length, requestId, std::move(mouthSchedule),
                job.leftDelayMs, job.rightDelayMs, job.leftGain, job.rightGain, job.threadId, playbackSlot });
        }
        // 3本のワーカーはそれぞれ別キューを監視しているため、
        // notify_one() では別スロットのワーカーだけが起きて通知を消費する。
        // 全ワーカーを起こし、自分のキューを持つワーカーに処理させる。
        audioCondition_.notify_all();
    }
}

void VoicevoxEngine::PlaybackLoop(size_t playbackSlot) {
    for (;;) {
        AudioItem item;
        {
            std::unique_lock<std::mutex> lock(audioMutex_);
            audioCondition_.wait(lock, [this, playbackSlot] {
                return stopping_ || !audioQueues_[playbackSlot].empty();
            });
            if (stopping_) return;
            item = std::move(audioQueues_[playbackSlot].front());
            audioQueues_[playbackSlot].pop_front();
        }
        if (item.requestId == RequestFor(item.threadId)) {
            const auto* wav = item.wav;
            if (item.length >= 44) {
                WAVEFORMATEX format{};
                format.wFormatTag = *reinterpret_cast<const WORD*>(wav + 20);
                const WORD sourceChannels = *reinterpret_cast<const WORD*>(wav + 22);
                format.nChannels = (item.leftDelayMs || item.rightDelayMs) ? 2 : sourceChannels;
                format.nSamplesPerSec = *reinterpret_cast<const DWORD*>(wav + 24);
                format.nAvgBytesPerSec = *reinterpret_cast<const DWORD*>(wav + 28);
                format.nBlockAlign = *reinterpret_cast<const WORD*>(wav + 32);
                format.wBitsPerSample = *reinterpret_cast<const WORD*>(wav + 34);
                if (format.nChannels != sourceChannels) {
                    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
                    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
                }
                HWAVEOUT device = nullptr;
                const MMRESULT openResult = waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
                if (openResult == MMSYSERR_NOERROR) {
                    std::vector<int16_t> spatialSamples;
                    const uint8_t* audioData = wav + 44;
                    DWORD audioBytes = static_cast<DWORD>(item.length - 44);
                    if ((item.leftDelayMs || item.rightDelayMs) && sourceChannels == 1 && format.wBitsPerSample == 16) {
                        const size_t samples = audioBytes / sizeof(int16_t);
                        const int sampleRate = static_cast<int>(format.nSamplesPerSec);
                        const size_t leftDelay = static_cast<size_t>(item.leftDelayMs * sampleRate / 1000);
                        const size_t rightDelay = static_cast<size_t>(item.rightDelayMs * sampleRate / 1000);
                        spatialSamples.assign((samples + max(leftDelay, rightDelay)) * 2, 0);
                        const auto* mono = reinterpret_cast<const int16_t*>(audioData);
                        for (size_t i = 0; i < samples; ++i) {
                            const auto clamp = [](double value) {
                                return static_cast<int16_t>(max(-32768.0, min(32767.0, value)));
                            };
                            spatialSamples[(i + leftDelay) * 2] = clamp(mono[i] * item.leftGain);
                            spatialSamples[(i + rightDelay) * 2 + 1] = clamp(mono[i] * item.rightGain);
                        }
                        audioData = reinterpret_cast<const uint8_t*>(spatialSamples.data());
                        audioBytes = static_cast<DWORD>(spatialSamples.size() * sizeof(int16_t));
                    }
                    WAVEHDR header{};
                    header.lpData = reinterpret_cast<LPSTR>(const_cast<uint8_t*>(audioData));
                    header.dwBufferLength = audioBytes;
                    const bool prepared = waveOutPrepareHeader(device, &header, sizeof(header)) == MMSYSERR_NOERROR;
                    if (prepared &&
                        waveOutWrite(device, &header, sizeof(header)) == MMSYSERR_NOERROR) {
                        Sleep(kLipSyncDelayMs);
                        const auto started = std::chrono::steady_clock::now();
                        size_t mouthIndex = 0;
                        double mouthEnd = 0.0;
                        NotifyLipSync(L'c');
                        while ((header.dwFlags & WHDR_DONE) == 0 && !stopping_ && item.requestId == RequestFor(item.threadId)) {
                            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                            if (mouthIndex < item.mouthSchedule.size() && elapsed >= mouthEnd) {
                                NotifyLipSync(item.mouthSchedule[mouthIndex].first);
                                mouthEnd += item.mouthSchedule[mouthIndex].second;
                                ++mouthIndex;
                            }
                            Sleep(5);
                        }
                        NotifyLipSync(L'c');
                    }
                    waveOutReset(device);
                    if (prepared) waveOutUnprepareHeader(device, &header, sizeof(header));
                    waveOutClose(device);
                }
                else {
                }
            }
            if (item.requestId == RequestFor(item.threadId)) Sleep(endSilenceMs_.load());
        }
        voicevox_wav_free(item.wav);
    }
}

void VoicevoxEngine::SetVoiceSettings(double speed, DWORD endSilenceMs)
{
    speed_.store(speed > 0.1 && speed < 10.0 ? speed : 1.0);
    endSilenceMs_.store(endSilenceMs <= 60000 ? endSilenceMs : 300);
}

void VoicevoxEngine::SetSpeaker(uint32_t speakerId)
{
    speakerId_.store(speakerId);
}

void VoicevoxEngine::SetSpatialPosition(int centerX, int screenWidth)
{
    if (screenWidth <= 0) { ClearSpatialPosition(); return; }
    // モニタ幅70cm・距離50cm。PS1から渡された実際の画面幅を物差しにする。
    const double screenCenter = static_cast<double>(screenWidth) / 2.0;
    const double x = (static_cast<double>(centerX) - screenCenter) / screenWidth * 0.70;
    const double ear = 0.09;
    const double distance = 0.50;
    const double left = std::sqrt(distance * distance + (x + ear) * (x + ear));
    const double right = std::sqrt(distance * distance + (x - ear) * (x - ear));
    const double difference = (left - right) / 343.0;
    // 検証用に、物理値を3倍して定位を強調する。
    const int delayMs = static_cast<int>(std::round(std::abs(difference) * 1000.0 * 3.0));
    spatialLeftDelayMs_.store(difference > 0 ? delayMs : 0);
    spatialRightDelayMs_.store(difference < 0 ? delayMs : 0);

    const double side = max(-1.0, min(1.0, (static_cast<double>(centerX) - screenCenter) / screenCenter));
    // 音量差db。
    const double volumeDifferenceDb = std::abs(side) * 4.0;
    const double quietGain = std::pow(10.0, -volumeDifferenceDb / 20.0);
    spatialLeftGain_.store(side < 0 ? 1.0 : quietGain);
    spatialRightGain_.store(side > 0 ? 1.0 : quietGain);
}

void VoicevoxEngine::ClearSpatialPosition()
{
    spatialLeftDelayMs_.store(0);
    spatialRightDelayMs_.store(0);
    spatialLeftGain_.store(1.0);
    spatialRightGain_.store(1.0);
}

bool VoicevoxEngine::SetVoiceModel(const std::filesystem::path& path)
{
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (path == modelPath_) return true;
    const auto utf8Path = path.string();
    VoicevoxVoiceModelFile* next = nullptr;
    if (!Ok(voicevox_voice_model_file_open(utf8Path.c_str(), &next), "open voice model", error_, errorMutex_)) return false;
    // モデル切り替えは合成処理と直列化するが、全体Cancelは行わない。
    // Cancelすると別スレッドIDの再生まで停止してしまう。

    VoicevoxLoadVoiceModelOptions load_voice_options{};
    load_voice_options.on_existing = VOICEVOX_ON_EXISTING_VOICE_MODEL_ID_SKIP;

    if (!Ok(voicevox_synthesizer_load_voice_model(synthesizer_, next, load_voice_options), "load voice model", error_, errorMutex_)) {
        voicevox_voice_model_file_delete(next);
        return false;
    }
    if (model_) voicevox_voice_model_file_delete(model_);
    model_ = next;
    modelPath_ = path;
    return true;
}

void VoicevoxEngine::Cancel() {
    requestId_++;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        textQueue_.clear();
        threadRequests_.clear();
        threadSlots_.clear();
        playbackSlotOwners_.fill(L"");
    }
    ClearAudioQueue();
    queueCondition_.notify_all();
    audioCondition_.notify_all();
}

void VoicevoxEngine::ClearAudioQueue() {
    std::lock_guard<std::mutex> lock(audioMutex_);
    for (auto& queue : audioQueues_) {
        for (auto& item : queue) if (item.wav) voicevox_wav_free(item.wav);
        queue.clear();
    }
}
