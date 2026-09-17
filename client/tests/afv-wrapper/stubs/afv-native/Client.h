#pragma once
// Observable offline double: no device, socket or native worker is ever created.
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "event2/event.h"
#include "afv-native/util/ChainedCallback.h"
namespace afv_native {
using log_fn = void (*)(const char *, const char *, int, const char *, void *);
inline std::mutex loggerMutex;
inline log_fn logger = nullptr;
inline void *loggerRef = nullptr;
inline void setLogger(log_fn fn, void *ref) {
    std::lock_guard<std::mutex> lock(loggerMutex);
    logger = fn; loggerRef = ref;
}
namespace afv {
enum class APISessionError { BadPassword, RejectedCredentials, ConnectionError, AuthTokenExpiryTimeInPast };
enum class VoiceSessionError { BadResponseFromAPIServer, Timeout, UDPChannelError };
namespace dto { struct Station { std::string Name; unsigned FrequencyAlias = 0, Frequency = 0; }; }
}
namespace audio {
struct Device { std::string name; };
struct AudioDevice {
    inline static std::map<int, Device> inputs{{0, {"Mic A"}}, {1, {"Mic B"}}};
    inline static std::map<int, Device> outputs{{0, {"Output A"}}, {1, {"Output B"}}};
    static auto getCompatibleInputDevicesForApi(int) { return inputs; }
    static auto getCompatibleOutputDevicesForApi(int) { return outputs; }
};
}
enum class ClientEventType { APIServerError, VoiceServerChannelError, VoiceServerError,
    StationAliasesUpdated, VoiceServerConnected, VoiceServerDisconnected, AudioError };
class Client {
public:
    inline static Client *instance = nullptr;
    inline static int destroyed = 0;
    event_base *base;
    util::ChainedCallback<void(ClientEventType, void *, void *)> ClientEventCallback;
    std::vector<afv::dto::Station> aliases;
    float gains[2]{};
    int microphone = 0, starts = 0, stops = 0, headsetCalls = 0;
    bool audioRunning = false;
    bool failMicrophone = false, failHeadset = false, failSpeaker = false;
    std::shared_ptr<audio::AudioDevice> microphoneDevice, headsetDevice, speakerDevice;
    bool balance = false, effects = true, squelch = false, split = false;
    float strength = 0;
    std::string input, headset, speaker;
    Client(event_base *b, unsigned, const std::string &) : base(b) { instance = this; ++base->clients; }
    ~Client() {
        if(base->dispatching || audioRunning) std::abort();
        --base->clients; ++destroyed; instance = nullptr;
    }
    void stopAudio() {
        ++stops; audioRunning = false;
        microphoneDevice.reset(); headsetDevice.reset(); speakerDevice.reset();
    }
    void startAudio() { startMicrophone(); startHeadset(); startSpeaker(); }
    void startMicrophone() {
        ++starts;
        microphoneDevice = failMicrophone || input.empty() ? nullptr : std::make_shared<audio::AudioDevice>();
        audioRunning = audioRunning || bool(microphoneDevice);
    }
    void startHeadset() {
        ++starts;
        headsetDevice = failHeadset || headset.empty() ? nullptr : std::make_shared<audio::AudioDevice>();
        audioRunning = audioRunning || bool(headsetDevice);
    }
    void startSpeaker() {
        ++starts;
        speakerDevice = failSpeaker || speaker.empty() ? nullptr : std::make_shared<audio::AudioDevice>();
        audioRunning = audioRunning || bool(speakerDevice);
    }
    std::shared_ptr<const audio::AudioDevice> getMicrophoneDevice() const { return microphoneDevice; }
    std::shared_ptr<const audio::AudioDevice> getHeadsetDevice() const { return headsetDevice; }
    std::shared_ptr<const audio::AudioDevice> getSpeakerDevice() const { return speakerDevice; }
    void setMicrophoneDevice(const std::string &v) { input = v; }
    void setHeadsetDevice(const std::string &v) { headset = v; }
    void setSpeakerDevice(const std::string &v) { speaker = v; }
    void setEnableInputFilters(bool) {}
    void setEnableOutputEffects(bool v) { effects = v; }
    void setEnableHfSquelch(bool v) { squelch = v; }
    void setAutoOutputGain(bool v) { balance = v; }
    void setAutoOutputGainStrength(float v) { strength = v; }
    void setMicrophoneVolume(int v) { microphone = v; }
    void setRadioGain(unsigned r, float v) { gains[r] = v; }
    void setOnHeadset(unsigned, bool) { ++headsetCalls; }
    void setSplitAudioChannels(bool v) { split = v; }
    void setTxRadio(unsigned) {}
    void setPtt(bool) {}
    void setRadioState(unsigned, unsigned) {}
    void setClientPosition(double, double, double, double) {}
    void setCallsign(const std::string &) {}
    void setCredentials(const std::string &, const std::string &) {}
    void connect() {} // deliberately offline
    void disconnect() {}
    bool getRxActive(unsigned) const { return true; }
    float getInputPeak() const { return 0; }
    auto getStationAliases() const { return aliases; }
};
}
