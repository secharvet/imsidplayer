#ifndef SIDPLAYER_H
#define SIDPLAYER_H

#include <string>
#include <memory>
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
    bool isPlaying() const { return m_playing; }
    bool isPaused() const { return m_paused; }
    
    std::string getCurrentFile() const { return m_currentFile; }
    std::string getTuneInfo() const { return m_tuneInfo; }
    
    // Contrôle du mute des voix
    void setVoiceMute(int voice, bool muted);
    bool isVoiceMuted(int voice) const;
    
    // Sélection de l'engine audio
    enum EngineType {
        ENGINE_AUDIO = 0,   // Engine #0 : toutes les voix (avec muting)
        ENGINE_VOICE0 = 1,  // Engine #1 : voix 1 seule
        ENGINE_VOICE1 = 2,   // Engine #2 : voix 2 seule
        ENGINE_VOICE2 = 3    // Engine #3 : voix 3 seule
    };
    void setAudioEngine(EngineType engine);
    EngineType getAudioEngine() const { return m_selectedEngine; }
    
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

    // 4 moteurs SID en parallèle :
    // Engine #0 → audio réel (toutes les voix)
    // Engine #1 → analyse voix 1 (voix 2+3 mutées)
    // Engine #2 → analyse voix 2 (voix 1+3 mutées)
    // Engine #3 → analyse voix 3 (voix 1+2 mutées)
    std::unique_ptr<sidplayfp> m_engineAudio;      // Engine #0
    std::unique_ptr<sidplayfp> m_engineVoice0;      // Engine #1
    std::unique_ptr<sidplayfp> m_engineVoice1;      // Engine #2
    std::unique_ptr<sidplayfp> m_engineVoice2;      // Engine #3
    
    std::unique_ptr<SidTune> m_tune;
    // Un builder par moteur (nécessaire pour que chaque moteur fonctionne indépendamment)
    std::unique_ptr<ReSIDfpBuilder> m_builderAudio;
    std::unique_ptr<ReSIDfpBuilder> m_builderVoice0;
    std::unique_ptr<ReSIDfpBuilder> m_builderVoice1;
    std::unique_ptr<ReSIDfpBuilder> m_builderVoice2;
    
    SDL_AudioDeviceID m_audioDevice;
    SDL_AudioSpec m_audioSpec;
    
    std::string m_currentFile;
    std::string m_tuneInfo;
    bool m_playing;
    bool m_paused;
    
    // État du mute pour chaque voix (pour l'engine audio)
    bool m_voice0Muted;
    bool m_voice1Muted;
    bool m_voice2Muted;
    
    // Engine sélectionné pour l'audio
    EngineType m_selectedEngine;
    
    // Buffers statiques pour les oscilloscopes (approche bas niveau C)
    float m_voice0Samples[OSCILLOSCOPE_SIZE];
    float m_voice1Samples[OSCILLOSCOPE_SIZE];
    float m_voice2Samples[OSCILLOSCOPE_SIZE];
    int m_writeIndex; // Index d'écriture circulaire
    
    static const int SAMPLE_RATE = 44100;
    static const int BUFFER_SIZE = 256;
};

#endif // SIDPLAYER_H

