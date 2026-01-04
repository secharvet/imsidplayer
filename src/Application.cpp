#include "Application.h"
#include "Utils.h"
#include "Config.h"
#include "Logger.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <functional>

namespace fs = std::filesystem;

#ifdef HAS_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif

Application::Application() 
    : m_window(nullptr), m_renderer(nullptr), m_config(Config::getInstance()),
      m_databaseOperation(DatabaseOperation::None), m_databaseProgress(0.0f),
      m_databaseCurrent(0), m_databaseTotal(0), m_shouldStopDatabaseThread(false) {
}

Application::~Application() {
    shutdown();
}

bool Application::initialize() {
    // Initialiser le système de logging en premier
    Logger::initialize();
    
    if (!initSDL()) {
        return false;
    }
    
    if (!loadConfig()) {
        return false;
    }
    
    if (!initBackground()) {
        LOG_WARNING("Background initialization failed, continuing without backgrounds");
    }
    
    // Initialiser les composants
    m_playlist.loadFromConfig(m_config);
    
    // Restaurer le fichier en cours
    if (!m_config.getCurrentFile().empty() && fs::exists(m_config.getCurrentFile())) {
        m_player.loadFile(m_config.getCurrentFile());
        PlaylistNode* foundNode = m_playlist.findNodeByPath(m_config.getCurrentFile());
        if (foundNode) {
            m_playlist.setCurrentNode(foundNode);
            m_playlist.setScrollToCurrent(true);
        }
    }
    
    // Restaurer les états des voix
    for (int i = 0; i < 3; i++) {
        m_player.setVoiceMute(i, !m_config.isVoiceActive(i));
    }
    
    // Créer DatabaseManager
    m_database = std::make_unique<DatabaseManager>();
    
    // Créer HistoryManager (charge automatiquement l'historique au démarrage)
    m_history = std::make_unique<HistoryManager>();
    
    // Créer UIManager
    m_uiManager = std::make_unique<UIManager>(m_player, m_playlist, *m_background, m_fileBrowser, *m_database, *m_history);
    if (!m_uiManager->initialize(m_window, m_renderer)) {
        LOG_ERROR("Impossible d'initialiser UIManager");
        return false;
    }
    
    // Lancer le chargement de la base de données en thread
    loadDatabaseAsync();
    
    return true;
}

bool Application::initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "Erreur SDL: " << SDL_GetError() << std::endl;
        return false;
    }
    
    fs::path configDir = getConfigDir();
    m_configPath = (configDir / "config.txt").string();
    m_config.load(m_configPath);
    
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    m_window = SDL_CreateWindow(
        "imSid Player",
        m_config.getWindowX(),
        m_config.getWindowY(),
        m_config.getWindowWidth(),
        m_config.getWindowHeight(),
        window_flags
    );
    
    if (!m_window) {
        LOG_ERROR("Impossible de créer la fenêtre SDL");
        SDL_Quit();
        return false;
    }
    
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        LOG_ERROR("Impossible de créer le renderer SDL");
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }
    
    return true;
}

bool Application::initBackground() {
#ifdef HAS_SDL2_IMAGE
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        LOG_ERROR("Erreur SDL_image: {}", IMG_GetError());
        return false;
    }
    
    m_background = std::make_unique<BackgroundManager>(m_renderer);
    m_background->setCurrentIndex(m_config.getBackgroundIndex());
    m_background->setShown(m_config.isBackgroundShown());
    m_background->setAlpha(m_config.getBackgroundAlpha());
    
    // Restaurer le background par nom si disponible
    if (!m_config.getBackgroundFilename().empty()) {
        m_background->loadImages();
        const auto& images = m_background->getImages();
        bool found = false;
        for (size_t i = 0; i < images.size(); i++) {
            if (images[i].filename == m_config.getBackgroundFilename()) {
                m_background->setCurrentIndex(i);
                found = true;
                break;
            }
        }
        if (!found && m_config.getBackgroundIndex() >= 0 && 
            m_config.getBackgroundIndex() < (int)images.size()) {
            m_background->setCurrentIndex(m_config.getBackgroundIndex());
        } else if (!found && !images.empty()) {
            m_background->setCurrentIndex(0);
        }
    } else {
        m_background->loadImages();
    }
    
    return true;
#else
    m_background = std::make_unique<BackgroundManager>(m_renderer);
    return true;
#endif
}

bool Application::loadConfig() {
    // La config est déjà chargée dans initSDL
    return true;
}

void Application::saveConfig() {
    // Sauvegarder la playlist
    m_playlist.saveToConfig(m_config);
    
    // Mettre à jour les valeurs de config
    m_config.setCurrentFile(m_player.getCurrentFile());
    
    if (m_background) {
        m_config.setBackgroundIndex(m_background->getCurrentIndex());
        const auto& images = m_background->getImages();
        int currentIndex = m_background->getCurrentIndex();
        if (currentIndex >= 0 && currentIndex < (int)images.size()) {
            m_config.setBackgroundFilename(images[currentIndex].filename);
        } else {
            m_config.setBackgroundFilename("");
        }
        m_config.setBackgroundShown(m_background->isShown());
        m_config.setBackgroundAlpha(m_background->getAlpha());
    }
    
    // Sauvegarder la position et taille de la fenêtre
    int winX, winY, winW, winH;
    SDL_GetWindowPosition(m_window, &winX, &winY);
    SDL_GetWindowSize(m_window, &winW, &winH);
    m_config.setWindowPos(winX, winY);
    m_config.setWindowSize(winW, winH);
    
    // Sauvegarder les états des voix
    for (int i = 0; i < 3; i++) {
        m_config.setVoiceActive(i, !m_player.isVoiceMuted(i));
    }
    
    // Sauvegarder dans le fichier
    m_config.save(m_configPath);
}

int Application::run() {
    bool running = true;
    
    while (running) {
        // Vérifier l'état de la fenêtre
        Uint32 windowFlags = SDL_GetWindowFlags(m_window);
        bool isMinimized = (windowFlags & SDL_WINDOW_MINIMIZED) != 0;
        
        // Si la fenêtre est minimisée, attendre plus longtemps pour économiser le CPU
        if (isMinimized) {
            SDL_Event event;
            // Utiliser WaitEventTimeout pour ne pas bloquer indéfiniment
            if (SDL_WaitEventTimeout(&event, 100)) {
                if (m_uiManager->handleEvent(event)) {
                    running = false;
                }
                if (event.type == SDL_DROPFILE) {
                    handleDropFile(event.drop.file);
                }
            }
            continue;
        }
        
        // Traiter les événements
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (m_uiManager->handleEvent(event)) {
                running = false;
            }
            
            if (event.type == SDL_DROPFILE) {
                handleDropFile(event.drop.file);
            }
        }
        
        // Gérer la demande d'indexation
        if (m_uiManager && m_uiManager->indexRequested()) {
            m_uiManager->indexRequested() = false;
            if (m_database && m_databaseOperation.load() == DatabaseOperation::None) {
                indexPlaylistAsync();
            }
        }
        
        // Vérifier si le chargement de la DB est terminé et lancer le rebuild cache
        static bool loadingDone = false;
        if (m_databaseOperation.load() == DatabaseOperation::Loading && !loadingDone) {
            // Vérifier si le thread est terminé en vérifiant le progrès
            if (m_databaseProgress.load() >= 1.0f) {
                // Attendre un peu pour être sûr que le thread a terminé
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (m_databaseThread.joinable()) {
                    m_databaseThread.join();
                }
                loadingDone = true;
                m_databaseOperation = DatabaseOperation::None;
                rebuildCacheAsync();
            }
        } else if (m_databaseOperation.load() != DatabaseOperation::Loading) {
            loadingDone = false; // Reset pour la prochaine fois
        }
        
        // Vérifier si l'indexation est terminée et lancer le rebuild cache
        static bool indexingDone = false;
        if (m_databaseOperation.load() == DatabaseOperation::Indexing && !indexingDone) {
            // Vérifier si le thread est terminé
            if (m_databaseProgress.load() >= 1.0f && !m_databaseThread.joinable()) {
                indexingDone = true;
                m_databaseOperation = DatabaseOperation::None;
                rebuildCacheAsync();
            }
        } else if (m_databaseOperation.load() != DatabaseOperation::Indexing) {
            indexingDone = false; // Reset pour la prochaine fois
        }
        
        m_uiManager->render();
        
        // Pas besoin de limiter le FPS manuellement :
        // - SDL gère déjà le VSync via SDL_RENDERER_PRESENTVSYNC
        // - SDL_RenderPresent() attend automatiquement le prochain rafraîchissement vertical
    }
    
    saveConfig();
    if (m_database) {
        m_database->save();
    }
    return 0;
}

void Application::handleDropFile(const char* filepath) {
    if (!filepath) return;
    
    fs::path path(filepath);
    
    // Vérifier d'abord si c'est une image
    if (fs::is_regular_file(path)) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::vector<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".gif"};
        
        if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) != imageExtensions.end()) {
            // C'est une image, l'ajouter au background manager
            if (m_background && m_background->addImageFromFile(filepath)) {
                LOG_INFO("Image ajoutée: {}", path.filename().string());
            }
            SDL_free((void*)filepath);
            return;
        }
    }
    
    // C'est un fichier ou dossier SID
    if (fs::is_directory(path)) {
        m_playlist.addDirectory(path);
        LOG_INFO("Dossier ajouté à la playlist: {}", path.filename().string());
        // Marquer que les filtres doivent être mis à jour et rafraîchir l'arbre
        if (m_uiManager) {
            m_uiManager->markFiltersNeedUpdate();
            m_uiManager->refreshPlaylistTree();
        }
    } else if (fs::is_regular_file(path)) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".sid") {
            m_playlist.addFile(path);
            LOG_INFO("Fichier ajouté à la playlist: {}", path.filename().string());
            // Marquer que les filtres doivent être mis à jour et rafraîchir l'arbre
            if (m_uiManager) {
                m_uiManager->markFiltersNeedUpdate();
                m_uiManager->refreshPlaylistTree();
            }
        }
    }
    
    SDL_free((void*)filepath);
}

void Application::shutdown() {
    // Arrêter le thread de base de données si en cours
    waitForDatabaseThread();
    
    if (m_uiManager) {
        m_uiManager->shutdown();
        m_uiManager.reset();
    }
    
    if (m_background) {
        m_background.reset();
    }
    
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    
#ifdef HAS_SDL2_IMAGE
    IMG_Quit();
#endif
    
    SDL_Quit();
    
    // Arrêter le logger en dernier
    Logger::shutdown();
}

void Application::loadDatabaseAsync() {
    waitForDatabaseThread();
    
    m_databaseOperation = DatabaseOperation::Loading;
    m_databaseProgress = 0.0f;
    m_databaseCurrent = 0;
    m_databaseTotal = 1;
    {
        std::lock_guard<std::mutex> lock(m_databaseStatusMutex);
        m_databaseStatusMessage = "Loading database...";
    }
    
    // Mettre à jour l'UI
    if (m_uiManager) {
        m_uiManager->setDatabaseOperationInProgress(true, "Loading database...", 0.0f);
    }
    
    m_databaseThread = std::thread([this]() {
        if (m_database) {
            m_database->load();
        }
        m_databaseProgress = 1.0f;
        
        // Mettre à jour l'UI
        if (m_uiManager) {
            m_uiManager->setDatabaseOperationInProgress(false);
        }
    });
}

void Application::indexPlaylistAsync() {
    waitForDatabaseThread();
    
    m_databaseOperation = DatabaseOperation::Indexing;
    m_databaseProgress = 0.0f;
    m_databaseCurrent = 0;
    
    auto allFiles = m_playlist.getAllFiles();
    m_databaseTotal = allFiles.size();
    
    {
        std::lock_guard<std::mutex> lock(m_databaseStatusMutex);
        m_databaseStatusMessage = "Indexing playlist...";
    }
    
    m_databaseThread = std::thread([this, allFiles]() {
        if (!m_database) return;
        
        int indexed = 0;
        int total = allFiles.size();
        
        for (size_t i = 0; i < allFiles.size(); ++i) {
            if (m_shouldStopDatabaseThread.load()) break;
            
            PlaylistNode* node = allFiles[i];
            if (!node || node->filepath.empty()) continue;
            
            m_databaseCurrent = i + 1;
            m_databaseProgress = static_cast<float>(i + 1) / total;
            
            std::string statusMsg = "Indexing: " + fs::path(node->filepath).filename().string();
            {
                std::lock_guard<std::mutex> lock(m_databaseStatusMutex);
                m_databaseStatusMessage = statusMsg;
            }
            
            // Mettre à jour l'UI
            if (m_uiManager) {
                m_uiManager->setDatabaseOperationInProgress(true, statusMsg, m_databaseProgress.load());
            }
            
            if (m_database->indexFile(node->filepath)) {
                indexed++;
            }
        }
        
        m_database->save();
        
        m_databaseOperation = DatabaseOperation::None;
        m_databaseProgress = 1.0f;
        {
            std::lock_guard<std::mutex> lock(m_databaseStatusMutex);
            m_databaseStatusMessage = "Indexing complete: " + std::to_string(indexed) + " files";
        }
        
        // Mettre à jour l'UI
        if (m_uiManager) {
            m_uiManager->setDatabaseOperationInProgress(false);
        }
        
        // Ne pas appeler rebuildCacheAsync() depuis le thread, le faire depuis le thread principal
        // Le thread principal détectera la fin de l'indexation et lancera le rebuild
    });
}

void Application::rebuildCacheAsync() {
    waitForDatabaseThread();
    
    m_databaseOperation = DatabaseOperation::RebuildingCache;
    m_databaseProgress = 0.0f;
    m_databaseCurrent = 0;
    
    auto allFiles = m_playlist.getAllFiles();
    m_databaseTotal = allFiles.size();
    
    {
        std::lock_guard<std::mutex> lock(m_databaseStatusMutex);
        m_databaseStatusMessage = "Rebuilding cache...";
    }
    
    // Mettre à jour l'UI depuis le thread principal
    if (m_uiManager) {
        m_uiManager->setDatabaseOperationInProgress(true, "Rebuilding cache...", 0.0f);
    }
    
    m_databaseThread = std::thread([this, allFiles]() {
        if (!m_uiManager) return;
        
        size_t cached = 0;
        int total = allFiles.size();
        
        for (size_t i = 0; i < allFiles.size(); ++i) {
            if (m_shouldStopDatabaseThread.load()) break;
            
            m_databaseCurrent = i + 1;
            m_databaseProgress = static_cast<float>(i + 1) / total;
            
            // Mettre à jour l'UI (thread-safe)
            if (m_uiManager) {
                m_uiManager->setDatabaseOperationInProgress(true, "Rebuilding cache...", m_databaseProgress.load());
            }
        }
        
        // Reconstruire le cache dans le thread de travail
        // Note: rebuildFilepathToHashCache() accède à m_database qui est thread-safe pour lecture
        m_uiManager->rebuildFilepathToHashCache();
        
        cached = allFiles.size(); // Approximation
        
        m_databaseOperation = DatabaseOperation::None;
        m_databaseProgress = 1.0f;
        {
            std::lock_guard<std::mutex> lock(m_databaseStatusMutex);
            m_databaseStatusMessage = "Cache rebuilt: " + std::to_string(cached) + " files";
        }
        
        // Mettre à jour l'UI (thread-safe)
        if (m_uiManager) {
            m_uiManager->setDatabaseOperationInProgress(false);
        }
    });
}

void Application::waitForDatabaseThread() {
    if (m_databaseThread.joinable()) {
        m_shouldStopDatabaseThread = true;
        m_databaseThread.join();
        m_shouldStopDatabaseThread = false;
    }
}

std::string Application::getDatabaseOperationStatus() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_databaseStatusMutex));
    return m_databaseStatusMessage;
}

float Application::getDatabaseOperationProgress() const {
    return m_databaseProgress.load();
}

