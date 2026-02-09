#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <map>
#include <mutex>

// Classe singleton pour gérer la configuration
class Config {
public:
    static Config& getInstance();
    
    // Charger la configuration depuis un fichier
    bool load(const std::string& filename = "config.txt");
    
    // Sauvegarder la configuration dans un fichier
    bool save(const std::string& filename);
    
    // Sauvegarder dans le fichier chargé précédemment (ou par défaut)
    bool save();
    
    // Getters/Setters pour les valeurs de configuration
    std::string getCurrentFile() const { std::lock_guard<std::mutex> lock(m_mutex); return m_currentFile; }
    void setCurrentFile(const std::string& file) { std::lock_guard<std::mutex> lock(m_mutex); m_currentFile = file; }
    
    int getBackgroundIndex() const { std::lock_guard<std::mutex> lock(m_mutex); return m_backgroundIndex; }
    void setBackgroundIndex(int index) { std::lock_guard<std::mutex> lock(m_mutex); m_backgroundIndex = index; }
    
    std::string getBackgroundFilename() const { std::lock_guard<std::mutex> lock(m_mutex); return m_backgroundFilename; }
    void setBackgroundFilename(const std::string& filename) { std::lock_guard<std::mutex> lock(m_mutex); m_backgroundFilename = filename; }
    
    std::string getSonglengthsPath() const { std::lock_guard<std::mutex> lock(m_mutex); return m_songlengthsPath; }
    void setSonglengthsPath(const std::string& path) { std::lock_guard<std::mutex> lock(m_mutex); m_songlengthsPath = path; }
    
    bool isBackgroundShown() const { std::lock_guard<std::mutex> lock(m_mutex); return m_backgroundShown; }
    void setBackgroundShown(bool shown) { std::lock_guard<std::mutex> lock(m_mutex); m_backgroundShown = shown; }
    
    bool isProgressBarAnimated() const { std::lock_guard<std::mutex> lock(m_mutex); return m_progressBarAnimated; }
    void setProgressBarAnimated(bool animated) { std::lock_guard<std::mutex> lock(m_mutex); m_progressBarAnimated = animated; }
    
    bool isStarRatingRainbow() const { std::lock_guard<std::mutex> lock(m_mutex); return m_starRatingRainbow; }
    void setStarRatingRainbow(bool rainbow) { std::lock_guard<std::mutex> lock(m_mutex); m_starRatingRainbow = rainbow; }
    
    int getStarRatingRainbowStep() const { std::lock_guard<std::mutex> lock(m_mutex); return m_starRatingRainbowStep; }
    void setStarRatingRainbowStep(int step) { 
        std::lock_guard<std::mutex> lock(m_mutex);
        m_starRatingRainbowStep = std::max(0, std::min(51, step)); // Clamp entre 0 et 51
    }
    
    int getStarRatingRainbowCycleFreq() const { std::lock_guard<std::mutex> lock(m_mutex); return m_starRatingRainbowCycleFreq; }
    void setStarRatingRainbowCycleFreq(int freq) { 
        std::lock_guard<std::mutex> lock(m_mutex);
        m_starRatingRainbowCycleFreq = std::max(0, std::min(20, freq)); // Clamp entre 0 et 20
    }
    
    int getStarRatingRainbowOffset() const { std::lock_guard<std::mutex> lock(m_mutex); return m_starRatingRainbowOffset; }
    void setStarRatingRainbowOffset(int offset) { 
        std::lock_guard<std::mutex> lock(m_mutex);
        m_starRatingRainbowOffset = std::max(0, std::min(255, offset)); // Clamp entre 0 et 255
    }
    
    bool isLoopEnabled() const { std::lock_guard<std::mutex> lock(m_mutex); return m_loopEnabled; }
    void setLoopEnabled(bool enabled) { std::lock_guard<std::mutex> lock(m_mutex); m_loopEnabled = enabled; }
    
    int getBackgroundAlpha() const { std::lock_guard<std::mutex> lock(m_mutex); return m_backgroundAlpha; }
    void setBackgroundAlpha(int alpha) { std::lock_guard<std::mutex> lock(m_mutex); m_backgroundAlpha = alpha; }
    
    int getWindowX() const { std::lock_guard<std::mutex> lock(m_mutex); return m_windowX; }
    int getWindowY() const { std::lock_guard<std::mutex> lock(m_mutex); return m_windowY; }
    int getWindowWidth() const { std::lock_guard<std::mutex> lock(m_mutex); return m_windowWidth; }
    int getWindowHeight() const { std::lock_guard<std::mutex> lock(m_mutex); return m_windowHeight; }
    void setWindowPos(int x, int y) { std::lock_guard<std::mutex> lock(m_mutex); m_windowX = x; m_windowY = y; }
    void setWindowSize(int w, int h) { std::lock_guard<std::mutex> lock(m_mutex); m_windowWidth = w; m_windowHeight = h; }
    
    // État des voix (Voice 1, 2, 3 actives)
    bool isVoiceActive(int voice) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (voice >= 0 && voice < 3) return m_voiceActive[voice];
        return true;
    }
    void setVoiceActive(int voice, bool active) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (voice >= 0 && voice < 3) m_voiceActive[voice] = active;
    }
    
#ifdef ENABLE_CLOUD_SAVE
    // Cloud Save
    // Note: m_cloudSaveEnabled peut être renommé ou gardé comme master switch
    // Pour l'instant on garde le flag global s'il est utilisé, mais on supprime les endpoints npoint
    bool isCloudSaveEnabled() const { std::lock_guard<std::mutex> lock(m_mutex); return m_cloudSaveEnabled; }
    void setCloudSaveEnabled(bool enabled) { std::lock_guard<std::mutex> lock(m_mutex); m_cloudSaveEnabled = enabled; }
    
    // Community Ratings (Supabase)
    bool isCommunityRatingsEnabled() const { std::lock_guard<std::mutex> lock(m_mutex); return m_communityRatingsEnabled; }
    void setCommunityRatingsEnabled(bool enabled) { std::lock_guard<std::mutex> lock(m_mutex); m_communityRatingsEnabled = enabled; }
    
    std::string getCommunityRatingsUsername() const { std::lock_guard<std::mutex> lock(m_mutex); return m_communityRatingsUsername; }
    void setCommunityRatingsUsername(const std::string& username) { std::lock_guard<std::mutex> lock(m_mutex); m_communityRatingsUsername = username; }
    
    std::string getSupabaseProjectUrl() const { std::lock_guard<std::mutex> lock(m_mutex); return m_supabaseProjectUrl; }
    void setSupabaseProjectUrl(const std::string& url) { std::lock_guard<std::mutex> lock(m_mutex); m_supabaseProjectUrl = url; }
    
    std::string getSupabaseAnonKey() const { std::lock_guard<std::mutex> lock(m_mutex); return m_supabaseAnonKey; }
    void setSupabaseAnonKey(const std::string& key) { std::lock_guard<std::mutex> lock(m_mutex); m_supabaseAnonKey = key; }
    
    std::string getSupabaseAccessToken() const { std::lock_guard<std::mutex> lock(m_mutex); return m_supabaseAccessToken; }
    void setSupabaseAccessToken(const std::string& token) { std::lock_guard<std::mutex> lock(m_mutex); m_supabaseAccessToken = token; }
    
    std::string getSupabaseRefreshToken() const { std::lock_guard<std::mutex> lock(m_mutex); return m_supabaseRefreshToken; }
    void setSupabaseRefreshToken(const std::string& token) { std::lock_guard<std::mutex> lock(m_mutex); m_supabaseRefreshToken = token; }
    
    std::string getSupabaseUserId() const { std::lock_guard<std::mutex> lock(m_mutex); return m_supabaseUserId; }
    void setSupabaseUserId(const std::string& userId) { std::lock_guard<std::mutex> lock(m_mutex); m_supabaseUserId = userId; }

    std::string getRecoveryCode() const { std::lock_guard<std::mutex> lock(m_mutex); return m_recoveryCode; }
    void setRecoveryCode(const std::string& code) { std::lock_guard<std::mutex> lock(m_mutex); m_recoveryCode = code; }
#endif
    
private:
    Config(); // Constructeur (implémenté dans Config.cpp)
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    mutable std::mutex m_mutex;
    std::string m_configFilePath; // Chemin du fichier de configuration chargé
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
    // Endpoints npoint supprimés
    
    // Community Ratings (Supabase)
    bool m_communityRatingsEnabled = false;  // Désactivé par défaut
    std::string m_communityRatingsUsername;
    std::string m_supabaseProjectUrl;  // URL du projet Supabase
    std::string m_supabaseAnonKey;     // Clé API publique (anon key)
    std::string m_supabaseAccessToken; // JWT token pour réutiliser l'authentification
    std::string m_supabaseRefreshToken; // Refresh token pour renouveler l'access_token
    std::string m_supabaseUserId;      // UUID de l'utilisateur authentifié
    std::string m_recoveryCode;        // Dernier code de récupération généré
#endif
};

#endif // CONFIG_H

