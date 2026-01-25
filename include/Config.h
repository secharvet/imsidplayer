#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <map>

// Classe singleton pour gérer la configuration
class Config {
public:
    static Config& getInstance();
    
    // Charger la configuration depuis un fichier
    bool load(const std::string& filename = "config.txt");
    
    // Sauvegarder la configuration dans un fichier
    bool save(const std::string& filename = "config.txt");
    
    // Getters/Setters pour les valeurs de configuration
    std::string getCurrentFile() const { return m_currentFile; }
    void setCurrentFile(const std::string& file) { m_currentFile = file; }
    
    int getBackgroundIndex() const { return m_backgroundIndex; }
    void setBackgroundIndex(int index) { m_backgroundIndex = index; }
    
    std::string getBackgroundFilename() const { return m_backgroundFilename; }
    void setBackgroundFilename(const std::string& filename) { m_backgroundFilename = filename; }
    
    std::string getSonglengthsPath() const { return m_songlengthsPath; }
    void setSonglengthsPath(const std::string& path) { m_songlengthsPath = path; }
    
    bool isBackgroundShown() const { return m_backgroundShown; }
    void setBackgroundShown(bool shown) { m_backgroundShown = shown; }
    
    bool isProgressBarAnimated() const { return m_progressBarAnimated; }
    void setProgressBarAnimated(bool animated) { m_progressBarAnimated = animated; }
    
    bool isStarRatingRainbow() const { return m_starRatingRainbow; }
    void setStarRatingRainbow(bool rainbow) { m_starRatingRainbow = rainbow; }
    
    int getStarRatingRainbowStep() const { return m_starRatingRainbowStep; }
    void setStarRatingRainbowStep(int step) { 
        m_starRatingRainbowStep = std::max(0, std::min(51, step)); // Clamp entre 0 et 51
    }
    
    int getStarRatingRainbowCycleFreq() const { return m_starRatingRainbowCycleFreq; }
    void setStarRatingRainbowCycleFreq(int freq) { 
        m_starRatingRainbowCycleFreq = std::max(0, std::min(20, freq)); // Clamp entre 0 et 20
    }
    
    int getStarRatingRainbowOffset() const { return m_starRatingRainbowOffset; }
    void setStarRatingRainbowOffset(int offset) { 
        m_starRatingRainbowOffset = std::max(0, std::min(255, offset)); // Clamp entre 0 et 255
    }
    
    bool isLoopEnabled() const { return m_loopEnabled; }
    void setLoopEnabled(bool enabled) { m_loopEnabled = enabled; }
    
    int getBackgroundAlpha() const { return m_backgroundAlpha; }
    void setBackgroundAlpha(int alpha) { m_backgroundAlpha = alpha; }
    
    int getWindowX() const { return m_windowX; }
    int getWindowY() const { return m_windowY; }
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }
    void setWindowPos(int x, int y) { m_windowX = x; m_windowY = y; }
    void setWindowSize(int w, int h) { m_windowWidth = w; m_windowHeight = h; }
    
    // État des voix (Voice 1, 2, 3 actives)
    bool isVoiceActive(int voice) const {
        if (voice >= 0 && voice < 3) return m_voiceActive[voice];
        return true;
    }
    void setVoiceActive(int voice, bool active) {
        if (voice >= 0 && voice < 3) m_voiceActive[voice] = active;
    }
    
#ifdef ENABLE_CLOUD_SAVE
    // Cloud Save
    bool isCloudSaveEnabled() const { return m_cloudSaveEnabled; }
    void setCloudSaveEnabled(bool enabled) { m_cloudSaveEnabled = enabled; }
    
    std::string getCloudRatingEndpoint() const { return m_cloudRatingEndpoint; }
    void setCloudRatingEndpoint(const std::string& endpoint) { m_cloudRatingEndpoint = endpoint; }
    
    std::string getCloudHistoryEndpoint() const { return m_cloudHistoryEndpoint; }
    void setCloudHistoryEndpoint(const std::string& endpoint) { m_cloudHistoryEndpoint = endpoint; }
#endif
    
private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    // Valeurs de configuration
    std::string m_currentFile;
    int m_backgroundIndex = 0;
    std::string m_backgroundFilename;
    std::string m_songlengthsPath; // Chemin vers Songlengths.md5
    bool m_backgroundShown = false;
    int m_backgroundAlpha = 128;
    bool m_progressBarAnimated = true; // Dégradé arc-en-ciel animé dans la barre de progression
    bool m_starRatingRainbow = false; // Cyclage arc-en-ciel des étoiles de notation
    int m_starRatingRainbowStep = 51; // Écart d'index entre les étoiles dans la palette (0-51)
    int m_starRatingRainbowCycleFreq = 2; // Fréquence de cyclage (0-20, cycle une frame sur N)
    int m_starRatingRainbowOffset = 0; // Offset à ajouter à chaque index de palette (0-255)
    bool m_loopEnabled = false; // État du loop (redémarrer automatiquement à la fin)
    int m_windowX = 100;
    int m_windowY = 100;
    int m_windowWidth = 1200;
    int m_windowHeight = 800;
    bool m_voiceActive[3] = {true, true, true}; // Par défaut toutes actives
    
#ifdef ENABLE_CLOUD_SAVE
    // Cloud Save
    bool m_cloudSaveEnabled = false;
    std::string m_cloudRatingEndpoint;
    std::string m_cloudHistoryEndpoint;
#endif
};

#endif // CONFIG_H

