#ifndef SIDPLAYER_H
#define SIDPLAYER_H

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/builders/residfp.h>
#include <SDL2/SDL.h>

class SidPlayer {
public:
    SidPlayer();
    ~SidPlayer();

    bool loadFile(const std::string& filepath);
    void play();
    void pause();
    void stop();
    void nextSong();
    void prevSong();
    bool isPlaying() const { return m_playing; }
    bool isPaused() const { return m_paused; }
    
    std::string getCurrentFile() const { return m_currentFile; }
    std::string getTuneInfo() const { return m_tuneInfo; }
    std::string getSidModel() const; // Retourne le modèle SID détecté (6581 ou 8580)
    int getCurrentSong() const { return m_currentSong; }
    float getPlaybackTime() const;
    
    // Contrôle du mute des voix
    void setVoiceMute(int voice, bool muted);
    bool isVoiceMuted(int voice) const;
    
    // Contrôle du moteur master vs mixage manuel
    void setUseMasterEngine(bool useMaster);
    bool isUsingMasterEngine() const { return m_useMasterEngine; }
    
    // Contrôle du loop
    void setLoop(bool loop) { m_loopEnabled = loop; }
    bool isLoopEnabled() const { return m_loopEnabled; }
    
    // Pour les oscilloscopes - accès direct aux buffers (pas de copies!)
    static const int OSCILLOSCOPE_SIZE = 256;
    const float* getVoiceBuffer(int voice) const { 
        if (voice == 0) return m_voice0Samples;
        if (voice == 1) return m_voice1Samples;
        if (voice == 2) return m_voice2Samples;
        return nullptr;
    }

private:
    void audioCallback(void* userdata, Uint8* stream, int len);
    static void audioCallbackWrapper(void* userdata, Uint8* stream, int len);
    void applyVoiceMuting(); // Fonction utilitaire pour appliquer le mute sur l'engine audio
    void applyAnalysisEngineMuting(); // Fonction utilitaire pour appliquer le mute sur les engines d'analyse
    void drainAudioBuffer(); // Draine le buffer audio pour éviter les clics
    void fadeOut(int samples); // Fade-out rapide pour éviter les clics
    void fadeIn(int samples); // Fade-in rapide au démarrage

    // 3 moteurs SID en parallèle pour l'analyse (mixage manuel pour l'audio) :
    // Engine #1 → analyse voix 1 (voix 2+3 mutées)
    // Engine #2 → analyse voix 2 (voix 1+3 mutées)
    // Engine #3 → analyse voix 3 (voix 1+2 mutées)
    // L'audio mixé est généré en additionnant les 3 voix manuellement
    std::unique_ptr<sidplayfp> m_engineVoice0;      // Engine #1
    std::unique_ptr<sidplayfp> m_engineVoice1;      // Engine #2
    std::unique_ptr<sidplayfp> m_engineVoice2;      // Engine #3
    
    // Moteur master : joue les 3 voix nativement (pour comparaison de fidélité)
    std::unique_ptr<sidplayfp> m_engineMaster;      // Engine Master
    
    std::unique_ptr<SidTune> m_tune;
    // Un builder par moteur (nécessaire pour que chaque moteur fonctionne indépendamment)
    std::unique_ptr<ReSIDfpBuilder> m_builderVoice0;
    std::unique_ptr<ReSIDfpBuilder> m_builderVoice1;
    std::unique_ptr<ReSIDfpBuilder> m_builderVoice2;
    std::unique_ptr<ReSIDfpBuilder> m_builderMaster;
    
    SDL_AudioDeviceID m_audioDevice;
    SDL_AudioSpec m_audioSpec;
    
    std::string m_currentFile;
    std::string m_tuneInfo;
    SidConfig::sid_model_t m_currentSidModel; // Modèle SID actuellement utilisé
    bool m_playing;
    bool m_paused;
    
    // Synchronisation pour l'arrêt propre
    std::atomic<bool> m_audioCallbackActive;
    std::atomic<bool> m_stopping;
    
    // État du sub-tune
    int m_currentSong;
    
    // État du mute pour chaque voix
    bool m_voice0Muted;
    bool m_voice1Muted;
    bool m_voice2Muted;
    
    // Buffers statiques pour les oscilloscopes (approche bas niveau C)
    float m_voice0Samples[OSCILLOSCOPE_SIZE];
    float m_voice1Samples[OSCILLOSCOPE_SIZE];
    float m_voice2Samples[OSCILLOSCOPE_SIZE];
    int m_writeIndex; // Index d'écriture circulaire
    
    // Fade-in pour éviter les glitches au démarrage
    int m_fadeInCounter; // Compteur d'échantillons pour le fondu à l'ouverture
    static const int FADE_IN_DURATION = 2000; // Environ 45ms à 44.1kHz
    
    // Buffers statiques pour le mixage audio (évite new/delete dans le callback)
    // Taille suffisante pour gérer les callbacks audio (BUFFER_SIZE = 256, mais on prend une marge)
    static const int MAX_AUDIO_BUFFER_SIZE = 512;
    int16_t m_voice0AudioBuffer[MAX_AUDIO_BUFFER_SIZE];
    int16_t m_voice1AudioBuffer[MAX_AUDIO_BUFFER_SIZE];
    int16_t m_voice2AudioBuffer[MAX_AUDIO_BUFFER_SIZE];
    int16_t m_masterAudioBuffer[MAX_AUDIO_BUFFER_SIZE]; // Buffer pour le moteur master
    
    // Flag pour basculer entre master et mixage manuel
    bool m_useMasterEngine;
    
    // Flag pour le loop (redémarrer automatiquement à la fin)
    bool m_loopEnabled;
    
    static const int SAMPLE_RATE = 44100;
    static const int BUFFER_SIZE = 256;
};

#endif // SIDPLAYER_H

