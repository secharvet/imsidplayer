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
    : m_playing(false)
    , m_paused(false)
    , m_audioDevice(0)
    , m_writeIndex(0)
    , m_voice0Muted(false)  // Par défaut, toutes les voix sont actives (non mutées)
    , m_voice1Muted(false)
    , m_voice2Muted(false)
    , m_audioCallbackActive(false)
    , m_stopping(false)
    , m_fadeInCounter(FADE_IN_DURATION) // Initialisé à la durée max pour désactiver le fade au démarrage
    , m_currentSidModel(SidConfig::MOS6581) // Par défaut : modèle 6581
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

    // Créer les 3 moteurs SID pour l'analyse et le mixage manuel
    m_engineVoice0 = std::make_unique<sidplayfp>(); // Engine #1 : analyse voix 1
    m_engineVoice1 = std::make_unique<sidplayfp>(); // Engine #2 : analyse voix 2
    m_engineVoice2 = std::make_unique<sidplayfp>(); // Engine #3 : analyse voix 3
    
    // Créer un builder ReSIDfp pour chaque moteur (nécessaire pour fonctionnement indépendant)
    m_builderVoice0 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice0");
    m_builderVoice1 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice1");
    m_builderVoice2 = std::make_unique<ReSIDfpBuilder>("ReSIDfp-Voice2");
    
    // Configurer chaque builder
    unsigned int maxSids = m_engineVoice0->info().maxsids();
    
    if (m_builderVoice0->create(maxSids) == 0 ||
        m_builderVoice1->create(maxSids) == 0 ||
        m_builderVoice2->create(maxSids) == 0) {
        std::cerr << "Erreur: Impossible de créer les builders ReSIDfp" << std::endl;
        return;
    }
    
    m_builderVoice0->filter(true);
    m_builderVoice1->filter(true);
    m_builderVoice2->filter(true);
    
    // Configuration commune
    SidConfig cfg;
    cfg.frequency = SAMPLE_RATE;
    cfg.playback = SidConfig::MONO;
    cfg.samplingMethod = SidConfig::INTERPOLATE;
    
    // Configurer Engine #1 (analyse voix 0)
    // mute(sid, voice, enable) : enable=true unmute (active), enable=false mute (désactive)
    // Note: les mute() doivent être appelés APRÈS le load() de la musique
    cfg.sidEmulation = m_builderVoice0.get();
    if (!m_engineVoice0->config(cfg)) {
        std::cerr << "Erreur de configuration engine voix 0: " << m_engineVoice0->error() << std::endl;
        return;
    }
    
    // Configurer Engine #2 (analyse voix 1)
    cfg.sidEmulation = m_builderVoice1.get();
    if (!m_engineVoice1->config(cfg)) {
        std::cerr << "Erreur de configuration engine voix 1: " << m_engineVoice1->error() << std::endl;
        return;
    }
    
    // Configurer Engine #3 (analyse voix 2)
    cfg.sidEmulation = m_builderVoice2.get();
    if (!m_engineVoice2->config(cfg)) {
        std::cerr << "Erreur de configuration engine voix 2: " << m_engineVoice2->error() << std::endl;
        return;
    }
}

SidPlayer::~SidPlayer() {
    stop();
    
    // Attendre que le callback audio soit complètement terminé
    // Le callback vérifie m_stopping et m_audioCallbackActive
    int maxWait = 100; // Maximum 100ms d'attente
    int waited = 0;
    while (m_audioCallbackActive.load() && waited < maxWait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waited++;
    }
    
    if (m_audioDevice > 0) {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }
    SDL_Quit();
}

bool SidPlayer::loadFile(const std::string& filepath) {
    stop();
    
    // Drainer le buffer audio pour éviter les clics lors du changement de fichier
    drainAudioBuffer();
    
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
    
    // Détecter le modèle SID depuis les métadonnées du fichier
    const SidTuneInfo* tuneInfo = m_tune->getInfo();
    SidConfig::sid_model_t sidModel = SidConfig::MOS6581; // Par défaut : modèle "Old" (C64 Fat)
    
    if (tuneInfo && tuneInfo->sidModel(0) == SidTuneInfo::SIDMODEL_8580) {
        sidModel = SidConfig::MOS8580; // Modèle "New" (C64 Slim)
    }
    
    // Stocker le modèle détecté pour l'affichage
    m_currentSidModel = sidModel;
    
    // Charger le tune dans les 3 moteurs
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
    // Engine 0 : voix 0 seule (muter voix 1 et 2)
    m_engineVoice0->mute(0, 1, false); // Muter voix 1
    m_engineVoice0->mute(0, 2, false); // Muter voix 2
    // Engine 1 : voix 1 seule (muter voix 0 et 2)
    m_engineVoice1->mute(0, 0, false); // Muter voix 0
    m_engineVoice1->mute(0, 2, false); // Muter voix 2
    // Engine 2 : voix 2 seule (muter voix 0 et 1)
    m_engineVoice2->mute(0, 0, false); // Muter voix 0
    m_engineVoice2->mute(0, 1, false); // Muter voix 1
    
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
    
    // Mettre à jour la fréquence d'échantillonnage et le modèle SID dans la config pour tous les moteurs
    // Chaque moteur doit utiliser sa propre config avec son propre builder
    SidConfig cfgVoice0 = m_engineVoice0->config();
    cfgVoice0.frequency = obtained.freq;
    cfgVoice0.defaultSidModel = sidModel; // Appliquer le modèle détecté depuis le fichier
    cfgVoice0.forceSidModel = true; // Forcer l'utilisation du modèle détecté
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
    cfgVoice1.defaultSidModel = sidModel; // Appliquer le modèle détecté depuis le fichier
    cfgVoice1.forceSidModel = true; // Forcer l'utilisation du modèle détecté
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
    cfgVoice2.defaultSidModel = sidModel; // Appliquer le modèle détecté depuis le fichier
    cfgVoice2.forceSidModel = true; // Forcer l'utilisation du modèle détecté
    cfgVoice2.sidEmulation = m_builderVoice2.get(); // Réassigner le builder
    if (!m_engineVoice2->config(cfgVoice2)) {
        std::cerr << "Erreur de configuration engine voix 2: " << m_engineVoice2->error() << std::endl;
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
        m_tune.reset();
        return false;
    }
    
    // Récupérer les infos de la chanson (réutiliser tuneInfo déjà déclaré)
    m_currentFile = filepath;
    // tuneInfo est déjà déclaré plus haut pour la détection du modèle SID
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
        // Si on était en train de jouer, arrêter et drainer le buffer pour éviter les clics
        if (m_playing) {
            SDL_PauseAudioDevice(m_audioDevice, 1);
            drainAudioBuffer();
        }
        
        // Réinitialiser la lecture depuis le début pour tous les moteurs
        m_engineVoice0->load(m_tune.get());
        m_engineVoice1->load(m_tune.get());
        m_engineVoice2->load(m_tune.get());
        
        // Réappliquer les muting pour les engines d'analyse
        // Engine 0 : voix 0 seule
        m_engineVoice0->mute(0, 1, false); // Muter voix 1
        m_engineVoice0->mute(0, 2, false); // Muter voix 2
        // Engine 1 : voix 1 seule
        m_engineVoice1->mute(0, 0, false); // Muter voix 0
        m_engineVoice1->mute(0, 2, false); // Muter voix 2
        // Engine 2 : voix 2 seule
        m_engineVoice2->mute(0, 0, false); // Muter voix 0
        m_engineVoice2->mute(0, 1, false); // Muter voix 1
        // 1. Reset explicite des moteurs (si disponible dans ta version de libsidplayfp)
        m_engineVoice0->stop(); // Parfois stop() réinitialise les niveaux
        
        // 2. IMPORTANT : Vide les buffers internes avant de laisser SDL lire
        // On génère quelques millisecondes de "vide" pour stabiliser le filtre ReSID
        int16_t dummy[512];
        m_engineVoice0->play(dummy, 512);
        m_engineVoice1->play(dummy, 512);
        m_engineVoice2->play(dummy, 512);

        // 3. Remet tes oscillos à 0 pour éviter la ligne plate au début
        for (int i = 0; i < OSCILLOSCOPE_SIZE; ++i) {
            m_voice0Samples[i] = 0.0f;
            m_voice1Samples[i] = 0.0f;
            m_voice2Samples[i] = 0.0f;
        }
        m_writeIndex = 0;
        
        // 4. Réinitialiser le compteur de fade-in pour déclencher le fondu au démarrage
        m_fadeInCounter = 0;
        
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
    if (m_audioDevice == 0) return;
    
    // Marquer qu'on est en train d'arrêter
    m_stopping = true;
    m_playing = false;
    m_paused = false;
    
    // Arrêter les engines
    if (m_engineVoice0) {
        m_engineVoice0->stop();
        m_engineVoice1->stop();
        m_engineVoice2->stop();
    }
    
    // Pauser l'audio device
    SDL_PauseAudioDevice(m_audioDevice, 1);
    
    // Drainer le buffer audio pour éviter les clics
    drainAudioBuffer();
    
    m_stopping = false;
}

void SidPlayer::setVoiceMute(int voice, bool muted) {
    if (voice < 0 || voice > 2) return;
    
    // Mettre à jour l'état
    if (voice == 0) m_voice0Muted = muted;
    else if (voice == 1) m_voice1Muted = muted;
    else if (voice == 2) m_voice2Muted = muted;
    
    // Appliquer le mute directement sur les engines
    // Chaque engine joue une seule voix isolée, mais on doit muter la voix dans tous les engines
    // pour que le mute prenne effet immédiatement
    if (m_tune) {  // Ne faire que si une musique est chargée
        if (voice == 0) {

            // Engine 0 : voix 0 seule (voix 1 et 2 toujours mutées pour l'isolation)
            m_engineVoice0->mute(0, 0, !muted); // !muted car enable=true unmute, false mute
            m_engineVoice0->mute(0, 1, false); // Voix 1 toujours mutée dans engine 0
            m_engineVoice0->mute(0, 2, false); // Voix 2 toujours mutée dans engine 0

        } else if (voice == 1) {
            // Engine 1 : voix 1 seule (voix 0 et 2 toujours mutées pour l'isolation)
            m_engineVoice1->mute(0, 0, false); // Voix 0 toujours mutée dans engine 1
            m_engineVoice1->mute(0, 1, !muted); // !muted car enable=true unmute, false mute
            m_engineVoice1->mute(0, 2, false); // Voix 2 toujours mutée dans engine 1

        } else if (voice == 2) {
            // Engine 2 : voix 2 seule (voix 0 et 1 toujours mutées pour l'isolation)
            m_engineVoice2->mute(0, 0, false); // Voix 0 toujours mutée dans engine 2
            m_engineVoice2->mute(0, 1, false); // Voix 1 toujours mutée dans engine 2
            m_engineVoice2->mute(0, 2, !muted); // !muted car enable=true unmute, false mute
        }
    }
    
    // Le mute est aussi géré dans audioCallback en remplissant avec des zéros pour le mixage
}

bool SidPlayer::isVoiceMuted(int voice) const {
    if (voice == 0) return m_voice0Muted;
    if (voice == 1) return m_voice1Muted;
    if (voice == 2) return m_voice2Muted;
    return false;
}

void SidPlayer::audioCallback(void* userdata, Uint8* stream, int len) {
    m_audioCallbackActive = true;
    
    // Si on est en train d'arrêter, remplir avec du silence et sortir
    if (m_stopping || !m_playing || m_paused) {
        SDL_memset(stream, 0, len);
        m_audioCallbackActive = false;
        return;
    }
    
    int16_t* mixBuffer = reinterpret_cast<int16_t*>(stream);
    int samples = len / sizeof(int16_t);

    // Utiliser les buffers statiques pour éviter new/delete
    // Générer les échantillons pour chaque voix (inconditionnel pour éviter la désynchronisation)
    m_engineVoice0->play(m_voice0AudioBuffer, samples);
    m_engineVoice1->play(m_voice1AudioBuffer, samples);
    m_engineVoice2->play(m_voice2AudioBuffer, samples);
    
    // --- APPLICATION DU FADE-IN ---
    // Applique un fondu progressif sur les premiers échantillons pour éviter les glitches
    if (m_fadeInCounter < FADE_IN_DURATION) {
        for (int i = 0; i < samples; ++i) {
            if (m_fadeInCounter < FADE_IN_DURATION) {
                float gain = static_cast<float>(m_fadeInCounter) / FADE_IN_DURATION;
                m_voice0AudioBuffer[i] = static_cast<int16_t>(m_voice0AudioBuffer[i] * gain);
                m_voice1AudioBuffer[i] = static_cast<int16_t>(m_voice1AudioBuffer[i] * gain);
                m_voice2AudioBuffer[i] = static_cast<int16_t>(m_voice2AudioBuffer[i] * gain);
                m_fadeInCounter++;
            } else {
                break; // Fade terminé
            }
        }
    }
    // ------------------------------
    
    // Si une voix est mutée, remplir son buffer avec des zéros (le mute() sur les engines ne fonctionne pas toujours pendant le play)
    if (m_voice0Muted) {
        SDL_memset(m_voice0AudioBuffer, 0, samples * sizeof(int16_t));
    }
    if (m_voice1Muted) {
        SDL_memset(m_voice1AudioBuffer, 0, samples * sizeof(int16_t));
    }
    if (m_voice2Muted) {
        SDL_memset(m_voice2AudioBuffer, 0, samples * sizeof(int16_t));
    }
    
    // Compter le nombre de voix actives
    int activeVoices = 0;
    if (!m_voice0Muted) activeVoices++;
    if (!m_voice1Muted) activeVoices++;
    if (!m_voice2Muted) activeVoices++;
    
    // Mixer les 3 voix manuellement (addition et division par le nombre de voix actives)
    if (activeVoices > 0) {
        for (int i = 0; i < samples; ++i) {
            int32_t sum = static_cast<int32_t>(m_voice0AudioBuffer[i]) + 
                          static_cast<int32_t>(m_voice1AudioBuffer[i]) + 
                          static_cast<int32_t>(m_voice2AudioBuffer[i]);
            mixBuffer[i] = static_cast<int16_t>(sum / activeVoices);
        }
    } else {
        // Toutes les voix sont mutées, silence complet
        SDL_memset(mixBuffer, 0, samples * sizeof(int16_t));
    }
    
    // Capturer les échantillons pour les oscilloscopes
    // Utiliser les buffers statiques déjà générés
    int samplesToCapture = (samples < 256) ? samples : 256; // OSCILLOSCOPE_SIZE = 256
    
    // Écrire dans les buffers circulaires
    for (int i = 0; i < samplesToCapture; ++i) {
        // Normaliser int16_t vers float [-1.0, 1.0]
        m_voice0Samples[m_writeIndex] = m_voice0AudioBuffer[i] / 32768.0f;
        m_voice1Samples[m_writeIndex] = m_voice1AudioBuffer[i] / 32768.0f;
        m_voice2Samples[m_writeIndex] = m_voice2AudioBuffer[i] / 32768.0f;
        
        m_writeIndex++;
        if (m_writeIndex >= OSCILLOSCOPE_SIZE) m_writeIndex = 0;
    }
    
    m_audioCallbackActive = false;
}

// Fonction supprimée - on capture maintenant directement dans audioCallback

// Fonction supprimée - accès direct via getVoiceBuffer() maintenant

void SidPlayer::drainAudioBuffer() {
    if (m_audioDevice == 0) return;
    
    // Attendre que le buffer audio soit vidé
    // SDL garde environ 2-3 buffers en avance, donc on attend quelques millisecondes
    // Temps = (taille_buffer * nombre_buffers) / fréquence_échantillonnage
    // Avec BUFFER_SIZE=256 et SAMPLE_RATE=44100, un buffer = ~5.8ms
    // On attend environ 3 buffers = ~17ms
    int bufferTimeMs = (BUFFER_SIZE * 1000) / SAMPLE_RATE;
    int waitTimeMs = bufferTimeMs * 3; // Attendre 3 buffers
    
    std::this_thread::sleep_for(std::chrono::milliseconds(waitTimeMs));
}

void SidPlayer::fadeOut(int samples) {
    // TODO: Implémentation future du fade-out
    // Pour l'instant, le drainAudioBuffer() suffit pour éviter les clics majeurs
    (void)samples; // Éviter le warning unused parameter
}

void SidPlayer::fadeIn(int samples) {
    // TODO: Implémentation future du fade-in
    // Nécessiterait un compteur dans audioCallback pour appliquer le fade progressif
    (void)samples; // Éviter le warning unused parameter
}

std::string SidPlayer::getSidModel() const {
    if (m_currentSidModel == SidConfig::MOS8580) {
        return "8580 (New SID)";
    } else {
        return "6581 (Old SID)";
    }
}

void SidPlayer::audioCallbackWrapper(void* userdata, Uint8* stream, int len) {
    SidPlayer* player = static_cast<SidPlayer*>(userdata);
    player->audioCallback(userdata, stream, len);
}

