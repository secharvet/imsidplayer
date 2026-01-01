#ifndef APPLICATION_H
#define APPLICATION_H

#include "SidPlayer.h"
#include "Config.h"
#include "PlaylistManager.h"
#include "BackgroundManager.h"
#include "FileBrowser.h"
#include "UIManager.h"
#include "DatabaseManager.h"
#include <SDL2/SDL.h>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>

enum class DatabaseOperation {
    None,
    Loading,
    Indexing,
    RebuildingCache
};

class Application {
public:
    Application();
    ~Application();
    
    // Initialiser l'application
    bool initialize();
    
    // Lancer la boucle principale
    int run();
    
    // Nettoyage
    void shutdown();
    
    // État des opérations de base de données
    DatabaseOperation getDatabaseOperation() const { return m_databaseOperation.load(); }
    std::string getDatabaseOperationStatus() const;
    float getDatabaseOperationProgress() const;
    
private:
    // SDL
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    
    // Composants
    SidPlayer m_player;
    Config& m_config;
    PlaylistManager m_playlist;
    std::unique_ptr<BackgroundManager> m_background;
    FileBrowser m_fileBrowser;
    std::unique_ptr<UIManager> m_uiManager;
    std::unique_ptr<DatabaseManager> m_database;
    
    // Configuration
    std::string m_configPath;
    
    // Threading pour opérations longues
    std::atomic<DatabaseOperation> m_databaseOperation;
    std::atomic<float> m_databaseProgress;
    std::atomic<int> m_databaseCurrent;
    std::atomic<int> m_databaseTotal;
    mutable std::string m_databaseStatusMessage;
    mutable std::mutex m_databaseStatusMutex;
    std::thread m_databaseThread;
    std::atomic<bool> m_shouldStopDatabaseThread;
    
    // Initialisation
    bool initSDL();
    bool initBackground();
    bool loadConfig();
    void saveConfig();
    
    // Gestion des événements
    bool handleEvent(const SDL_Event& event);
    void handleDropFile(const char* filepath);
    
    // Threading
    void loadDatabaseAsync();
    void indexPlaylistAsync();
    void rebuildCacheAsync();
    void waitForDatabaseThread();
};

#endif // APPLICATION_H

