#include "SidPlayer.h"
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTuneInfo.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

SidPlayer::SidPlayer() 
    : m_playing(false)
    , m_paused(false)
    , m_audioDevice(0)
    , m_writeIndex(0)
    , m_voice0Muted(false)  // Par défaut, toutes les voix sont actives (non mutées)
    , m_voice1Muted(false)
    , m_voice2Muted(false)
    , m_selectedEngine(ENGINE_AUDIO)  // Par défaut, utiliser l'engine audio mixé
{
    // Initialiser les buffers à zéro
    for (int i = 0; i < OSCILLOSCOPE_SIZE; ++i) {
        m_voice0Samples[i] = 0.0f;
        m_voice1Samples[i] = 0.0f;
        m_voice2Samples[i] = 0.0f;
    }
    // Initialiser SDL audio
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "Erreur SDL: " << SDL_GetError() << std::endl;
        return;
    }

    // Créer les 4 moteurs SID
    m_engineAudio = std::make_unique<sidplayfp>();  // Engine #0 : audio réel
    m_engineVoice0 = std::make_unique<sidplayfp>(); // Engine #1 : analyse voix 1
    m_engineVoice1 = std::make_unique<sidplayfp>(); // Engine #2 : analyse voix 2
    m_engineVoice2 = std::make_unique<sidplayfp>(); // Engine #3 : analyse voix 3
    
    // Créer un builder ReSIDfp pour chaque moteur (nécessaire pour fonctionnement indépendant)
    m_builderAudio = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Audio");
    m_builderVoice0 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice0");
    m_builderVoice1 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice1");
    m_builderVoice2 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice2");
    
    // Configurer chaque builder
    unsigned int maxSids = m_engineAudio->info().maxsids();
    
    if (m_builderAudio->create(maxSids) == 0 ||
        m_builderVoice0->create(maxSids) == 0 ||
        m_builderVoice1->create(maxSids) == 0 ||
        m_builderVoice2->create(maxSids) == 0) {
        std::cerr << "Erreur: Impossible de créer les builders ReSIDfp" << std::endl;
        return;
    }
    
    m_builderAudio->filter(true);
    m_builderVoice0->filter(true);
    m_builderVoice1->filter(true);
    m_builderVoice2->filter(true);
    
    // Configuration commune
    SidConfig cfg;
    cfg.frequency = SAMPLE_RATE;
    cfg.playback = SidConfig::MONO;
    cfg.samplingMethod = SidConfig::INTERPOLATE;
    
    // Configurer Engine #0 (audio réel - toutes les voix actives)
    cfg.sidEmulation = m_builderAudio.get();
    if (!m_engineAudio->config(cfg)) {
        std::cerr << "Erreur de configuration engine audio: " << m_engineAudio->error() << std::endl;
        return;
    }
    
    // Configurer Engine #1 (analyse voix 1 - muter voix 2 et 3)
    cfg.sidEmulation = m_builderVoice0.get();
    if (!m_engineVoice0->config(cfg)) {
        std::cerr << "Erreur de configuration engine voix 0: " << m_engineVoice0->error() << std::endl;
        return;
    }
    // Le muting sera appliqué via applyAnalysisEngineMuting()
    
    // Configurer Engine #2 (analyse voix 2 - muter voix 1 et 3)
    cfg.sidEmulation = m_builderVoice1.get();
    if (!m_engineVoice1->config(cfg)) {
        std::cerr << "Erreur de configuration engine voix 1: " << m_engineVoice1->error() << std::endl;
        return;
    }
    
    // Configurer Engine #3 (analyse voix 3 - muter voix 1 et 2)
    cfg.sidEmulation = m_builderVoice2.get();
    if (!m_engineVoice2->config(cfg)) {
        std::cerr << "Erreur de configuration engine voix 2: " << m_engineVoice2->error() << std::endl;
        return;
    }
    
    // Appliquer le muting initial sur les engines d'analyse
    applyAnalysisEngineMuting();
}

SidPlayer::~SidPlayer() {
    stop();
    if (m_audioDevice > 0) {
        SDL_CloseAudioDevice(m_audioDevice);
    }
    SDL_Quit();
}

bool SidPlayer::loadFile(const std::string& filepath) {
    stop();
    
    // Fermer l'ancien device audio s'il existe
    if (m_audioDevice > 0) {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }
    
    // Charger le fichier SID
    m_tune = std::make_unique<SidTune>(filepath.c_str());
    
    if (!m_tune->getStatus()) {
        std::cerr << "Erreur: Impossible de charger le fichier SID" << std::endl;
        m_tune.reset();
        return false;
    }
    
    // Sélectionner la première sous-chanson (les chansons commencent à 1)
    m_tune->selectSong(1);
    
    // Charger le tune dans les 4 moteurs
    if (!m_engineAudio->load(m_tune.get())) {
        std::cerr << "Erreur: Impossible de charger la chanson dans le moteur audio" << std::endl;
        m_tune.reset();
        return false;
    }
    if (!m_engineVoice0->load(m_tune.get())) {
        std::cerr << "Erreur: Impossible de charger la chanson dans le moteur voix 0" << std::endl;
        m_tune.reset();
        return false;
    }
    if (!m_engineVoice1->load(m_tune.get())) {
        std::cerr << "Erreur: Impossible de charger la chanson dans le moteur voix 1" << std::endl;
        m_tune.reset();
        return false;
    }
    if (!m_engineVoice2->load(m_tune.get())) {
        std::cerr << "Erreur: Impossible de charger la chanson dans le moteur voix 2" << std::endl;
        m_tune.reset();
        return false;
    }
    
    // Réappliquer les muting après le load
    applyAnalysisEngineMuting();
    
    // Appliquer le mute des voix sur l'engine audio
    applyVoiceMuting();
    
    // Configurer l'audio SDL
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = BUFFER_SIZE;
    desired.callback = audioCallbackWrapper;
    desired.userdata = this;
    
    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (m_audioDevice == 0) {
        std::cerr << "Erreur SDL Audio: " << SDL_GetError() << std::endl;
        m_tune.reset();
        return false;
    }
    
    m_audioSpec = obtained;
    
    // Mettre à jour la fréquence d'échantillonnage dans la config pour tous les moteurs
    // Chaque moteur doit utiliser sa propre config avec son propre builder
    SidConfig cfgAudio = m_engineAudio->config();
    cfgAudio.frequency = obtained.freq;
    cfgAudio.sidEmulation = m_builderAudio.get(); // Réassigner le builder
    if (!m_engineAudio->config(cfgAudio)) {
        std::cerr << "Erreur de configuration engine audio: " << m_engineAudio->error() << std::endl;
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
        m_tune.reset();
        return false;
    }
    
    SidConfig cfgVoice0 = m_engineVoice0->config();
    cfgVoice0.frequency = obtained.freq;
    cfgVoice0.sidEmulation = m_builderVoice0.get(); // Réassigner le builder
    if (!m_engineVoice0->config(cfgVoice0)) {
        std::cerr << "Erreur de configuration engine voix 0: " << m_engineVoice0->error() << std::endl;
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
        m_tune.reset();
        return false;
    }
    
    SidConfig cfgVoice1 = m_engineVoice1->config();
    cfgVoice1.frequency = obtained.freq;
    cfgVoice1.sidEmulation = m_builderVoice1.get(); // Réassigner le builder
    if (!m_engineVoice1->config(cfgVoice1)) {
        std::cerr << "Erreur de configuration engine voix 1: " << m_engineVoice1->error() << std::endl;
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
        m_tune.reset();
        return false;
    }
    
    SidConfig cfgVoice2 = m_engineVoice2->config();
    cfgVoice2.frequency = obtained.freq;
    cfgVoice2.sidEmulation = m_builderVoice2.get(); // Réassigner le builder
    if (!m_engineVoice2->config(cfgVoice2)) {
        std::cerr << "Erreur de configuration engine voix 2: " << m_engineVoice2->error() << std::endl;
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
        m_tune.reset();
        return false;
    }
    
    // Récupérer les infos de la chanson
    m_currentFile = filepath;
    const SidTuneInfo* tuneInfo = m_tune->getInfo();
    if (tuneInfo && tuneInfo->numberOfInfoStrings() > 0) {
        m_tuneInfo = "";
        for (unsigned int i = 0; i < tuneInfo->numberOfInfoStrings(); ++i) {
            const char* info = tuneInfo->infoString(i);
            if (info && strlen(info) > 0) {
                if (!m_tuneInfo.empty()) {
                    m_tuneInfo += " | ";
                }
                m_tuneInfo += info;
            }
        }
        if (m_tuneInfo.empty()) {
            m_tuneInfo = "Aucune information disponible";
        }
    } else {
        m_tuneInfo = "Aucune information disponible";
    }
    
    return true;
}

void SidPlayer::play() {
    if (!m_tune || m_audioDevice == 0) return;
    
    if (m_paused) {
        SDL_PauseAudioDevice(m_audioDevice, 0);
        m_paused = false;
    } else {
        // Réinitialiser la lecture depuis le début pour tous les moteurs
        m_engineAudio->load(m_tune.get());
        m_engineVoice0->load(m_tune.get());
        m_engineVoice1->load(m_tune.get());
        m_engineVoice2->load(m_tune.get());
        
        // Réappliquer les muting pour les engines d'analyse
        applyAnalysisEngineMuting();
        
        // Réappliquer le mute des voix sur l'engine audio
        applyVoiceMuting();
        
        SDL_PauseAudioDevice(m_audioDevice, 0);
    }
    m_playing = true;
}

void SidPlayer::pause() {
    if (m_playing && !m_paused) {
        SDL_PauseAudioDevice(m_audioDevice, 1);
        m_paused = true;
    }
}

void SidPlayer::stop() {
    SDL_PauseAudioDevice(m_audioDevice, 1);
    m_playing = false;
    m_paused = false;
    if (m_engineAudio) {
        m_engineAudio->stop();
        m_engineVoice0->stop();
        m_engineVoice1->stop();
        m_engineVoice2->stop();
    }
}

void SidPlayer::applyVoiceMuting() {
    // Appliquer le mute des voix sur l'engine audio
    // mute(0, voice, false) = muter, mute(0, voice, true) = unmute
    if (m_engineAudio) {
        m_engineAudio->mute(0, 0, !m_voice0Muted);
        m_engineAudio->mute(0, 1, !m_voice1Muted);
        m_engineAudio->mute(0, 2, !m_voice2Muted);
    }
}

void SidPlayer::applyAnalysisEngineMuting() {
    // Appliquer le mute sur les engines d'analyse pour isoler chaque voix
    // Engine Voice0 : muter voix 2 et 3 (garder voix 1)
    if (m_engineVoice0) {
        m_engineVoice0->mute(0, 0, true);  // Unmute voix 1
        m_engineVoice0->mute(0, 1, false); // Muter voix 2
        m_engineVoice0->mute(0, 2, false); // Muter voix 3
    }
    
    // Engine Voice1 : muter voix 1 et 3 (garder voix 2)
    if (m_engineVoice1) {
        m_engineVoice1->mute(0, 0, false); // Muter voix 1
        m_engineVoice1->mute(0, 1, true);  // Unmute voix 2
        m_engineVoice1->mute(0, 2, false); // Muter voix 3
    }
    
    // Engine Voice2 : muter voix 1 et 2 (garder voix 3)
    if (m_engineVoice2) {
        m_engineVoice2->mute(0, 0, false); // Muter voix 1
        m_engineVoice2->mute(0, 1, false); // Muter voix 2
        m_engineVoice2->mute(0, 2, true);  // Unmute voix 3
    }
}

void SidPlayer::setVoiceMute(int voice, bool muted) {
    if (voice < 0 || voice > 2) return;
    
    // Mettre à jour l'état
    if (voice == 0) m_voice0Muted = muted;
    else if (voice == 1) m_voice1Muted = muted;
    else if (voice == 2) m_voice2Muted = muted;
    
    // Appliquer immédiatement le mute sur l'engine audio
    // Le mute ne s'applique que sur ENGINE_AUDIO
    // Les autres engines sont déjà configurés pour isoler une seule voix
    applyVoiceMuting();
}

bool SidPlayer::isVoiceMuted(int voice) const {
    if (voice == 0) return m_voice0Muted;
    if (voice == 1) return m_voice1Muted;
    if (voice == 2) return m_voice2Muted;
    return false;
}

void SidPlayer::setAudioEngine(EngineType engine) {
    if (engine >= ENGINE_AUDIO && engine <= ENGINE_VOICE2) {
        m_selectedEngine = engine;
        
        // Si on passe à ENGINE_AUDIO, réappliquer les muting
        if (engine == ENGINE_AUDIO) {
            applyVoiceMuting();
        }
    }
}

void SidPlayer::audioCallback(void* userdata, Uint8* stream, int len) {
    if (!m_playing || m_paused) {
        SDL_memset(stream, 0, len);
        return;
    }
    
    // Générer les échantillons audio selon l'engine sélectionné
    int16_t* mixBuffer = reinterpret_cast<int16_t*>(stream);
    int samples = len / sizeof(int16_t);
    
    // Utiliser l'engine sélectionné
    sidplayfp* engineToUse = nullptr;
    switch (m_selectedEngine) {
        case ENGINE_AUDIO:
            engineToUse = m_engineAudio.get();
            // Réappliquer le mute au cas où (pour l'engine audio)
            applyVoiceMuting();
            break;
        case ENGINE_VOICE0:
            engineToUse = m_engineVoice0.get();
            // Réappliquer le mute pour s'assurer que seules les voix correctes sont actives
            if (m_engineVoice0) {
                m_engineVoice0->mute(0, 0, true);  // Unmute voix 1
                m_engineVoice0->mute(0, 1, false); // Muter voix 2
                m_engineVoice0->mute(0, 2, false); // Muter voix 3
            }
            break;
        case ENGINE_VOICE1:
            engineToUse = m_engineVoice1.get();
            if (m_engineVoice1) {
                m_engineVoice1->mute(0, 0, false); // Muter voix 1
                m_engineVoice1->mute(0, 1, true);  // Unmute voix 2
                m_engineVoice1->mute(0, 2, false); // Muter voix 3
            }
            break;
        case ENGINE_VOICE2:
            engineToUse = m_engineVoice2.get();
            if (m_engineVoice2) {
                m_engineVoice2->mute(0, 0, false); // Muter voix 1
                m_engineVoice2->mute(0, 1, false); // Muter voix 2
                m_engineVoice2->mute(0, 2, true);  // Unmute voix 3
            }
            break;
    }
    
    if (engineToUse) {
        engineToUse->play(mixBuffer, samples);
    } else {
        SDL_memset(stream, 0, len);
    }
    
    // Capturer les échantillons pour les oscilloscopes depuis les 3 engines d'analyse
    // Ne capturer que tous les N callbacks pour éviter de surcharger
    static int frameSkip = 0;
   // if (++frameSkip < 4) return; // Capturer tous les 4 callbacks
   // frameSkip = 0;
    
    // Capturer OSCILLOSCOPE_SIZE échantillons
    int samplesToCapture = OSCILLOSCOPE_SIZE;
    
    // Buffers temporaires pour chaque voix
    int16_t voice0Buffer[OSCILLOSCOPE_SIZE];
    int16_t voice1Buffer[OSCILLOSCOPE_SIZE];
    int16_t voice2Buffer[OSCILLOSCOPE_SIZE];
    
    // Générer les échantillons pour chaque voix isolée
    m_engineVoice0->play(voice0Buffer, samplesToCapture);
    m_engineVoice1->play(voice1Buffer, samplesToCapture);
    m_engineVoice2->play(voice2Buffer, samplesToCapture);
    
    // Écrire dans les buffers circulaires
    for (int i = 0; i < samplesToCapture; ++i) {
        // Normaliser int16_t vers float [-1.0, 1.0]
        m_voice0Samples[m_writeIndex] = voice0Buffer[i] / 32768.0f;
        m_voice1Samples[m_writeIndex] = voice1Buffer[i] / 32768.0f;
        m_voice2Samples[m_writeIndex] = voice2Buffer[i] / 32768.0f;
        
        m_writeIndex++;
        if (m_writeIndex >= OSCILLOSCOPE_SIZE) m_writeIndex = 0;
    }

}

// Fonction supprimée - on capture maintenant directement dans audioCallback

// Fonction supprimée - accès direct via getVoiceBuffer() maintenant

void SidPlayer::audioCallbackWrapper(void* userdata, Uint8* stream, int len) {
    SidPlayer* player = static_cast<SidPlayer*>(userdata);
    player->audioCallback(userdata, stream, len);
}

