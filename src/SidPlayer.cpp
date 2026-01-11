#include "SidPlayer.h"
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTuneInfo.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <chrono>

SidPlayer::SidPlayer() 
    : m_playing(false), m_paused(false), m_audioDevice(0), m_writeIndex(0), m_currentSong(0),
      m_voice0Muted(false), m_voice1Muted(false), m_voice2Muted(false),
      m_audioCallbackActive(false), m_stopping(false), m_fadeInCounter(FADE_IN_DURATION),
      m_currentSidModel(SidConfig::MOS6581), m_useMasterEngine(false), m_loopEnabled(false)
{
    for (int i = 0; i < OSCILLOSCOPE_SIZE; ++i) {
        m_voice0Samples[i] = 0.0f; m_voice1Samples[i] = 0.0f; m_voice2Samples[i] = 0.0f;
    }
    if (SDL_Init(SDL_INIT_AUDIO) < 0) { return; }
    m_engineVoice0 = std::make_unique<sidplayfp>();
    m_engineVoice1 = std::make_unique<sidplayfp>();
    m_engineVoice2 = std::make_unique<sidplayfp>();
    m_engineMaster = std::make_unique<sidplayfp>();
    m_builderVoice0 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice0");
    m_builderVoice1 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice1");
    m_builderVoice2 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice2");
    m_builderMaster = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Master");
    unsigned int maxSids = m_engineVoice0->info().maxsids();
    m_builderVoice0->create(maxSids); m_builderVoice1->create(maxSids); m_builderVoice2->create(maxSids); m_builderMaster->create(maxSids);
    m_builderVoice0->filter(true); m_builderVoice1->filter(true); m_builderVoice2->filter(true); m_builderMaster->filter(true);
    SidConfig cfg;
    cfg.frequency = SAMPLE_RATE; cfg.playback = SidConfig::MONO; cfg.samplingMethod = SidConfig::RESAMPLE_INTERPOLATE;
    cfg.sidEmulation = m_builderVoice0.get(); m_engineVoice0->config(cfg);
    cfg.sidEmulation = m_builderVoice1.get(); m_engineVoice1->config(cfg);
    cfg.sidEmulation = m_builderVoice2.get(); m_engineVoice2->config(cfg);
    cfg.sidEmulation = m_builderMaster.get(); m_engineMaster->config(cfg);
}

SidPlayer::~SidPlayer() {
    stop();
    int maxWait = 100, waited = 0;
    while (m_audioCallbackActive.load() && waited < maxWait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waited++;
    }
    if (m_audioDevice > 0) { SDL_CloseAudioDevice(m_audioDevice); m_audioDevice = 0; }
    SDL_Quit();
}

bool SidPlayer::loadFile(const std::string& filepath) {
    stop();
    drainAudioBuffer();
    if (m_audioDevice > 0) { SDL_CloseAudioDevice(m_audioDevice); m_audioDevice = 0; }
    m_tune = std::make_unique<SidTune>(filepath.c_str());
    if (!m_tune->getStatus()) { m_tune.reset(); return false; }
    m_tune->selectSong(1);
    m_currentSong = 0;
    const SidTuneInfo* tuneInfo = m_tune->getInfo();
    SidConfig::sid_model_t sidModel = (tuneInfo && tuneInfo->sidModel(0) == SidTuneInfo::SIDMODEL_8580) ? SidConfig::MOS8580 : SidConfig::MOS6581;
    m_currentSidModel = sidModel;
    m_engineVoice0->load(m_tune.get()); m_engineVoice1->load(m_tune.get()); m_engineVoice2->load(m_tune.get()); m_engineMaster->load(m_tune.get());
    m_engineVoice0->mute(0, 1, false); m_engineVoice0->mute(0, 2, false);
    m_engineVoice1->mute(0, 0, false); m_engineVoice1->mute(0, 2, false);
    m_engineVoice2->mute(0, 0, false); m_engineVoice2->mute(0, 1, false);
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE; desired.format = AUDIO_S16SYS; desired.channels = 1; desired.samples = BUFFER_SIZE;
    desired.callback = audioCallbackWrapper; desired.userdata = this;
    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (m_audioDevice == 0) { m_tune.reset(); return false; }
    m_audioSpec = obtained;
    SidConfig cfg;
    cfg.frequency = obtained.freq; cfg.defaultSidModel = sidModel; cfg.forceSidModel = true;
    cfg.sidEmulation = m_builderVoice0.get(); m_engineVoice0->config(cfg);
    cfg.sidEmulation = m_builderVoice1.get(); m_engineVoice1->config(cfg);
    cfg.sidEmulation = m_builderVoice2.get(); m_engineVoice2->config(cfg);
    cfg.sidEmulation = m_builderMaster.get(); m_engineMaster->config(cfg);
    m_currentFile = filepath;
    m_tuneInfo = "No info available";
    if (tuneInfo && tuneInfo->numberOfInfoStrings() > 0) {
        std::string tempInfo;
        for (unsigned int i = 0; i < tuneInfo->numberOfInfoStrings(); ++i) {
            const char* info = tuneInfo->infoString(i);
            if (info && strlen(info) > 0) {
                if (!tempInfo.empty()) tempInfo += " | ";
                tempInfo += info;
            }
        }
        if (!tempInfo.empty()) m_tuneInfo = tempInfo;
    }
    return true;
}

void SidPlayer::play() {
    if (!m_tune || m_audioDevice == 0) return;
    if (m_paused) {
        SDL_PauseAudioDevice(m_audioDevice, 0);
        m_paused = false;
    } else {
        if (m_playing) { SDL_PauseAudioDevice(m_audioDevice, 1); drainAudioBuffer(); }
        m_engineVoice0->load(m_tune.get()); m_engineVoice1->load(m_tune.get());
        m_engineVoice2->load(m_tune.get()); m_engineMaster->load(m_tune.get());
        m_engineVoice0->stop(); m_engineVoice1->stop(); m_engineVoice2->stop(); m_engineMaster->stop();
        int16_t dummy[512];
        m_engineVoice0->play(dummy, 512); m_engineVoice1->play(dummy, 512);
        m_engineVoice2->play(dummy, 512); m_engineMaster->play(dummy, 512);
        m_engineVoice0->mute(0, 0, false); m_engineVoice0->mute(0, 1, true); m_engineVoice0->mute(0, 2, true);
        m_engineVoice1->mute(0, 0, true); m_engineVoice1->mute(0, 1, false); m_engineVoice1->mute(0, 2, true);
        m_engineVoice2->mute(0, 0, true); m_engineVoice2->mute(0, 1, true); m_engineVoice2->mute(0, 2, false);
        for (int i = 0; i < OSCILLOSCOPE_SIZE; ++i) { m_voice0Samples[i] = 0.0f; m_voice1Samples[i] = 0.0f; m_voice2Samples[i] = 0.0f; }
        m_writeIndex = 0;
        m_fadeInCounter = 0;
        SDL_PauseAudioDevice(m_audioDevice, 0);
    }
    m_playing = true;
}

void SidPlayer::pause() {
    if (m_playing && !m_paused) { SDL_PauseAudioDevice(m_audioDevice, 1); m_paused = true; }
}

void SidPlayer::stop() {
    if (m_audioDevice == 0) return;
    m_stopping = true; m_playing = false; m_paused = false;
    if (m_engineVoice0) { m_engineVoice0->stop(); m_engineVoice1->stop(); m_engineVoice2->stop(); m_engineMaster->stop(); }
    SDL_PauseAudioDevice(m_audioDevice, 1);
    drainAudioBuffer();
    m_stopping = false;
}

void SidPlayer::nextSong() {
    if (!m_tune) return;
    const SidTuneInfo* info = m_tune->getInfo();
    if (!info) return;
    int numSongs = info->songs();
    if (numSongs > 1) {
        m_currentSong = (m_currentSong + 1) % numSongs;
        m_tune->selectSong(m_currentSong + 1);
        play();
    }
}

void SidPlayer::prevSong() {
    if (!m_tune) return;
    const SidTuneInfo* info = m_tune->getInfo();
    if (!info) return;
    int numSongs = info->songs();
    if (numSongs > 1) {
        m_currentSong = (m_currentSong - 1 + numSongs) % numSongs;
        m_tune->selectSong(m_currentSong + 1);
        play();
    }
}

void SidPlayer::setVoiceMute(int voice, bool muted) {
    if (voice < 0 || voice > 2) return;
    if (voice == 0) m_voice0Muted = muted; else if (voice == 1) m_voice1Muted = muted; else if (voice == 2) m_voice2Muted = muted;
    if (m_tune) {
        m_engineMaster->mute(0, 0, m_voice0Muted); m_engineMaster->mute(0, 1, m_voice1Muted); m_engineMaster->mute(0, 2, m_voice2Muted);
        if (voice == 0) { m_engineVoice0->mute(0, 0, m_voice0Muted); m_engineVoice0->mute(0, 1, true); m_engineVoice0->mute(0, 2, true); }
        else if (voice == 1) { m_engineVoice1->mute(0, 0, true); m_engineVoice1->mute(0, 1, m_voice1Muted); m_engineVoice1->mute(0, 2, true); }
        else if (voice == 2) { m_engineVoice2->mute(0, 0, true); m_engineVoice2->mute(0, 1, true); m_engineVoice2->mute(0, 2, m_voice2Muted); }
    }
}

bool SidPlayer::isVoiceMuted(int voice) const {
    if (voice == 0) return m_voice0Muted; if (voice == 1) return m_voice1Muted; if (voice == 2) return m_voice2Muted; return false;
}

void SidPlayer::audioCallback(void* userdata, Uint8* stream, int len) {
    m_audioCallbackActive = true;
    if (m_stopping || !m_playing || m_paused) { SDL_memset(stream, 0, len); m_audioCallbackActive = false; return; }
    int16_t* mixBuffer = reinterpret_cast<int16_t*>(stream);
    int samples = len / sizeof(int16_t);
    int activeVoices = !m_voice0Muted + !m_voice1Muted + !m_voice2Muted;
    m_engineVoice0->play(m_voice0AudioBuffer, samples); m_engineVoice1->play(m_voice1AudioBuffer, samples); m_engineVoice2->play(m_voice2AudioBuffer, samples);
    if (m_voice0Muted) SDL_memset(m_voice0AudioBuffer, 0, samples * sizeof(int16_t));
    if (m_voice1Muted) SDL_memset(m_voice1AudioBuffer, 0, samples * sizeof(int16_t));
    if (m_voice2Muted) SDL_memset(m_voice2AudioBuffer, 0, samples * sizeof(int16_t));
    if (m_fadeInCounter < FADE_IN_DURATION) {
        for (int i = 0; i < samples && m_fadeInCounter < FADE_IN_DURATION; ++i, m_fadeInCounter++) {
            float gain = static_cast<float>(m_fadeInCounter) / FADE_IN_DURATION;
            m_voice0AudioBuffer[i] = static_cast<int16_t>(m_voice0AudioBuffer[i] * gain);
            m_voice1AudioBuffer[i] = static_cast<int16_t>(m_voice1AudioBuffer[i] * gain);
            m_voice2AudioBuffer[i] = static_cast<int16_t>(m_voice2AudioBuffer[i] * gain);
            m_masterAudioBuffer[i] = static_cast<int16_t>(m_masterAudioBuffer[i] * gain);
        }
    }
    if (m_useMasterEngine) {
        m_engineMaster->play(m_masterAudioBuffer, samples);
        SDL_memcpy(mixBuffer, m_masterAudioBuffer, samples * sizeof(int16_t));
    } else {
        m_engineMaster->play(m_masterAudioBuffer, samples);
        if (activeVoices > 0) {
            for (int i = 0; i < samples; ++i) {
                int32_t sum = static_cast<int32_t>(m_voice0AudioBuffer[i]) + static_cast<int32_t>(m_voice1AudioBuffer[i]) + static_cast<int32_t>(m_voice2AudioBuffer[i]);
                mixBuffer[i] = static_cast<int16_t>(std::clamp(sum, -32768, 32767));
            }
        } else { SDL_memset(mixBuffer, 0, samples * sizeof(int16_t)); }
    }
    int samplesToCapture = std::min(samples, 256);
    for (int i = 0; i < samplesToCapture; ++i) {
        m_voice0Samples[m_writeIndex] = m_voice0AudioBuffer[i] / 32768.0f;
        m_voice1Samples[m_writeIndex] = m_voice1AudioBuffer[i] / 32768.0f;
        m_voice2Samples[m_writeIndex] = m_voice2AudioBuffer[i] / 32768.0f;
        m_writeIndex = (m_writeIndex + 1) % OSCILLOSCOPE_SIZE;
    }
    m_audioCallbackActive = false;
}

std::string SidPlayer::getSidModel() const {
    return (m_currentSidModel == SidConfig::MOS8580) ? "8580 (New SID)" : "6581 (Old SID)";
}

void SidPlayer::setUseMasterEngine(bool useMaster) {
    m_useMasterEngine = useMaster;
}

void SidPlayer::audioCallbackWrapper(void* userdata, Uint8* stream, int len) {
    static_cast<SidPlayer*>(userdata)->audioCallback(userdata, stream, len);
}

void SidPlayer::drainAudioBuffer() {
    if (m_audioDevice == 0) return;
    int bufferTimeMs = (BUFFER_SIZE * 1000) / SAMPLE_RATE;
    std::this_thread::sleep_for(std::chrono::milliseconds(bufferTimeMs * 3));
}

float SidPlayer::getPlaybackTime() const {
    if (m_engineMaster) {
        return m_engineMaster->time();
    }
    return 0.0f;
}
