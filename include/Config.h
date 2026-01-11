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
    
    int getBackgroundAlpha() const { return m_backgroundAlpha; }
    void setBackgroundAlpha(int alpha) { m_backgroundAlpha = alpha; }
    
    int getWindowX() const { return m_windowX; }
    int getWindowY() const { return m_windowY; }
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }
    void setWindowPos(int x, int y) { m_windowX = x; m_windowY = y; }
    void setWindowSize(int w, int h) { m_windowWidth = w; m_windowHeight = h; }
    
    // Playlist (structure hiérarchique)
    struct PlaylistItem {
        std::string name;
        std::string path;
        bool isFolder;
        std::vector<PlaylistItem> children;
    };
    
    const std::vector<PlaylistItem>& getPlaylist() const { return m_playlist; }
    void setPlaylist(const std::vector<PlaylistItem>& playlist) { m_playlist = playlist; }
    
    // État des voix (Voice 1, 2, 3 actives)
    bool isVoiceActive(int voice) const {
        if (voice >= 0 && voice < 3) return m_voiceActive[voice];
        return true;
    }
    void setVoiceActive(int voice, bool active) {
        if (voice >= 0 && voice < 3) m_voiceActive[voice] = active;
    }
    
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
    int m_windowX = 100;
    int m_windowY = 100;
    int m_windowWidth = 1200;
    int m_windowHeight = 800;
    std::vector<PlaylistItem> m_playlist;
    bool m_voiceActive[3] = {true, true, true}; // Par défaut toutes actives
    
    // Parsing du fichier de config
    void parseLine(const std::string& line, int indent, std::vector<PlaylistItem>* currentList);
    void writePlaylist(std::ofstream& file, const std::vector<PlaylistItem>& items, int indent = 0);
};

#endif // CONFIG_H

