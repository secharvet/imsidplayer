#include "Application.h"
#include "Version.h"
#include "SongLengthDB.h"
#include "Utils.h"
#include "Config.h"
#include "Logger.h"
#ifdef ENABLE_CLOUD_SAVE
#include "CloudSyncManager.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"
#include "imgui.h"
#endif
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <cstring>
#include <chrono>

namespace fs = std::filesystem;

#ifdef HAS_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif

Application::Application() 
    : m_window(nullptr), m_renderer(nullptr), m_config(Config::getInstance()),
      m_databaseOperation(DatabaseOperation::None), m_databaseProgress(0.0f),
      m_databaseCurrent(0), m_databaseTotal(0), m_shouldStopDatabaseThread(false)
#ifdef ENABLE_CLOUD_SAVE
      , m_updateInProgress(false)
#endif
{
}

Application::~Application() {
    shutdown();
}

bool Application::initialize() {
    auto initStart = std::chrono::high_resolution_clock::now();
    
    // Initialiser le système de logging en premier
    auto loggerStart = std::chrono::high_resolution_clock::now();
    Logger::initialize();
    auto loggerEnd = std::chrono::high_resolution_clock::now();
    auto loggerTime = std::chrono::duration_cast<std::chrono::milliseconds>(loggerEnd - loggerStart).count();
    LOG_INFO("[App Init] Logger::initialize(): {} ms", loggerTime);
    
    // Initialiser SDL
    auto sdlStart = std::chrono::high_resolution_clock::now();
    if (!initSDL()) {
        return false;
    }
    auto sdlEnd = std::chrono::high_resolution_clock::now();
    auto sdlTime = std::chrono::duration_cast<std::chrono::milliseconds>(sdlEnd - sdlStart).count();
    LOG_INFO("[App Init] initSDL(): {} ms", sdlTime);
    
    // Charger la config (déjà fait dans initSDL, mais on log quand même)
    auto configStart = std::chrono::high_resolution_clock::now();
    if (!loadConfig()) {
        return false;
    }
    auto configEnd = std::chrono::high_resolution_clock::now();
    auto configTime = std::chrono::duration_cast<std::chrono::milliseconds>(configEnd - configStart).count();
    LOG_INFO("[App Init] loadConfig(): {} ms", configTime);
    
    // Initialiser le background
    auto bgStart = std::chrono::high_resolution_clock::now();
    if (!initBackground()) {
        LOG_WARNING("Background initialization failed, continuing without backgrounds");
    }
    auto bgEnd = std::chrono::high_resolution_clock::now();
    auto bgTime = std::chrono::duration_cast<std::chrono::milliseconds>(bgEnd - bgStart).count();
    LOG_INFO("[App Init] initBackground(): {} ms", bgTime);
    
    // Créer DatabaseManager et charger la DB en premier
    auto dbCreateStart = std::chrono::high_resolution_clock::now();
    m_database = std::make_unique<DatabaseManager>();
    auto dbCreateEnd = std::chrono::high_resolution_clock::now();
    auto dbCreateTime = std::chrono::duration_cast<std::chrono::milliseconds>(dbCreateEnd - dbCreateStart).count();
    LOG_INFO("[App Init] DatabaseManager creation: {} ms", dbCreateTime);
    
    auto preDbLoadEnd = std::chrono::high_resolution_clock::now();
    auto preDbLoadTime = std::chrono::duration_cast<std::chrono::milliseconds>(preDbLoadEnd - initStart).count();
    LOG_INFO("[App Init] Time before DB load: {} ms", preDbLoadTime);
    
    auto dbLoadStart = std::chrono::high_resolution_clock::now();
    if (m_database->load()) {
        auto dbLoadEnd = std::chrono::high_resolution_clock::now();
        auto dbLoadTime = std::chrono::duration_cast<std::chrono::milliseconds>(dbLoadEnd - dbLoadStart).count();
        LOG_INFO("[App Init] DatabaseManager::load() completed in {} ms ({} files)", dbLoadTime, m_database->getCount());
    }
    
    // Reconstruire la playlist depuis la DB
    auto playlistRebuildStart = std::chrono::high_resolution_clock::now();
    m_playlist.rebuildFromDatabase(*m_database);
    auto playlistRebuildEnd = std::chrono::high_resolution_clock::now();
    auto playlistRebuildTime = std::chrono::duration_cast<std::chrono::milliseconds>(playlistRebuildEnd - playlistRebuildStart).count();
    LOG_INFO("[App Init] PlaylistManager::rebuildFromDatabase() completed in {} ms", playlistRebuildTime);
    
    // Charger Songlengths.md5 si configuré
    if (!m_config.getSonglengthsPath().empty()) {
        SongLengthDB& db = SongLengthDB::getInstance();
        if (db.load(m_config.getSonglengthsPath())) {
            LOG_INFO("Songlengths.md5 loaded at startup: {} entries", db.getCount());
        } else {
            LOG_WARNING("Failed to load Songlengths.md5 at startup: {}", m_config.getSonglengthsPath());
        }
    }
    
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
    
    // Restaurer l'état du loop
    m_player.setLoop(m_config.isLoopEnabled());
    
    // Créer HistoryManager (charge automatiquement l'historique au démarrage)
    m_history = std::make_unique<HistoryManager>();
    
    // Créer RatingManager (charge automatiquement les ratings au démarrage)
    m_ratingManager = std::make_unique<RatingManager>();
    
#ifdef ENABLE_CLOUD_SAVE
    // Initialiser CloudSyncManager
    auto& cloudSync = CloudSyncManager::getInstance();
    if (cloudSync.initialize(m_ratingManager.get(), m_history.get())) {
        // Charger les endpoints depuis Config
        std::string ratingEndpoint = m_config.getCloudRatingEndpoint();
        std::string historyEndpoint = m_config.getCloudHistoryEndpoint();
        if (!ratingEndpoint.empty()) {
            cloudSync.setRatingEndpoint(ratingEndpoint);
        }
        if (!historyEndpoint.empty()) {
            cloudSync.setHistoryEndpoint(historyEndpoint);
        }
        // Activer si configuré
        if (m_config.isCloudSaveEnabled()) {
            cloudSync.setEnabled(true);
            // Pull automatique au démarrage pour récupérer les dernières notes
            cloudSync.pullRatings();
        }
        LOG_INFO("CloudSyncManager initialized");
    } else {
        LOG_WARNING("Failed to initialize CloudSyncManager");
    }
#endif
    
    // Créer UIManager
    m_uiManager = std::make_unique<UIManager>(m_player, m_playlist, *m_background, m_fileBrowser, *m_database, *m_history, *m_ratingManager);
    if (!m_uiManager->initialize(m_window, m_renderer)) {
        LOG_ERROR("Impossible d'initialiser UIManager");
        return false;
    }
    
#ifdef ENABLE_CLOUD_SAVE
    // Configurer le callback pour rendre le dialog de mise à jour
    m_uiManager->setUpdateDialogCallback([this]() {
        renderUpdateDialog();
    });
#endif
    
    // Lancer la reconstruction du cache en arrière-plan
    rebuildCacheAsync();
    
    // Nettoyer les fichiers .old au démarrage
#ifdef ENABLE_CLOUD_SAVE
    UpdateInstaller::cleanupOldFiles();
    
    // Vérifier les mises à jour en arrière-plan (non-bloquant)
    checkForUpdatesAsync();
#endif
    
    return true;
}

bool Application::initSDL() {
#ifdef _WIN32
    // Préférer le driver OpenGL sous Windows/Wine pour éviter les erreurs de swapchain D3D
    // et améliorer la stabilité lors du redimensionnement de la fenêtre.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        LOG_CRITICAL_MSG("Erreur SDL: {}", SDL_GetError());
        return false;
    }
    
    fs::path configDir = getConfigDir();
    m_configPath = (configDir / "config.txt").string();
    m_config.load(m_configPath);
    
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    
    // Titre avec version
    std::string windowTitle = VERSION_STRING_FULL;
    
    m_window = SDL_CreateWindow(
        windowTitle.c_str(),
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
    
    // Essayer d'abord avec accélération matérielle
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        LOG_WARNING("Échec du renderer accéléré, tentative avec renderer logiciel...");
        // Fallback vers renderer logiciel (utile sous Wine ou si pas de GPU)
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
        if (!m_renderer) {
            LOG_ERROR("Impossible de créer le renderer SDL (ni accéléré ni logiciel)");
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            return false;
        }
        LOG_INFO("Utilisation du renderer logiciel");
    }
    
    return true;
}

bool Application::recreateRenderer() {
    LOG_INFO("Recréation du renderer SDL...");
    
    // Détruire l'ancien renderer
    if (m_renderer) {
        // Shutdown ImGui backend avant de détruire le renderer
        if (m_uiManager) {
            m_uiManager->shutdownRenderer();
        }
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    
    // Recréer le renderer (essayer accéléré d'abord, puis logiciel)
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        LOG_WARNING("Échec du renderer accéléré, tentative avec renderer logiciel...");
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
        if (!m_renderer) {
            LOG_ERROR("Impossible de recréer le renderer SDL: {}", SDL_GetError());
            return false;
        }
        LOG_INFO("Utilisation du renderer logiciel");
    }
    
    // Réinitialiser le backend ImGui avec le nouveau renderer
    if (m_uiManager) {
        if (!m_uiManager->reinitializeRenderer(m_renderer)) {
            LOG_ERROR("Impossible de réinitialiser le backend ImGui");
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
            return false;
        }
    }
    
    // Réinitialiser le background manager avec le nouveau renderer
    if (m_background) {
        m_background->setRenderer(m_renderer);
    }
    
    LOG_INFO("Renderer recréé avec succès");
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
            // Gérer les événements de fenêtre
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_EXPOSED || 
                    event.window.event == SDL_WINDOWEVENT_RESTORED ||
                    event.window.event == SDL_WINDOWEVENT_SHOWN) {
                    // Vérifier la validité du renderer sur ces événements critiques
                    if (m_renderer) {
                        SDL_ClearError();
                        int outputWidth = 0, outputHeight = 0;
                        if (SDL_GetRendererOutputSize(m_renderer, &outputWidth, &outputHeight) < 0) {
                            const char* error = SDL_GetError();
                            if (error && strlen(error) > 0) {
                                LOG_WARNING("Renderer invalide détecté ({}), tentative de recréation...", error);
                                if (!recreateRenderer()) {
                                    LOG_ERROR("Échec de la recréation du renderer");
                                    running = false;
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
            
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
        
        // Vérifier périodiquement si le renderer est toujours valide
        // (toutes les 1000 frames environ, soit ~16 secondes à 60 FPS)
        static int frameCount = 0;
        frameCount++;
        if (frameCount % 1000 == 0 && m_renderer) {
            SDL_ClearError();
            // Essayer de récupérer une propriété du renderer pour vérifier qu'il est valide
            int outputWidth = 0, outputHeight = 0;
            if (SDL_GetRendererOutputSize(m_renderer, &outputWidth, &outputHeight) < 0) {
                const char* error = SDL_GetError();
                if (error && strlen(error) > 0) {
                    LOG_WARNING("Renderer invalide détecté (frame {}): {}, tentative de recréation...", frameCount, error);
                    if (!recreateRenderer()) {
                        LOG_ERROR("Échec de la recréation du renderer");
                        running = false;
                        continue;
                    }
                }
            }
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
    
    // Vérifier d'abord si c'est Songlengths.md5
    if (fs::is_regular_file(path)) {
        std::string filename = path.filename().string();
        std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
        
        if (filename == "songlengths.md5") {
            // Charger le fichier Songlengths.md5
            SongLengthDB& db = SongLengthDB::getInstance();
            if (db.load(filepath)) {
                // Sauvegarder le chemin dans la config
                m_config.setSonglengthsPath(filepath);
                saveConfig();
                LOG_INFO("Songlengths.md5 loaded: {} entries", db.getCount());
            } else {
                LOG_ERROR("Failed to load Songlengths.md5: {}", filepath);
            }
            SDL_free((void*)filepath);
            return;
        }
        
        // Vérifier si c'est une image
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
        // Détection automatique de HVSC (présence de Songlengths.md5)
        fs::path slPath;
        
        // 1. Chercher à la racine du dossier déposé
        if (fs::exists(path / "Songlengths.md5")) slPath = path / "Songlengths.md5";
        else if (fs::exists(path / "songlengths.md5")) slPath = path / "songlengths.md5";
        // 2. Chercher dans le sous-dossier DOCUMENTS (standard HVSC)
        else if (fs::exists(path / "DOCUMENTS" / "Songlengths.md5")) slPath = path / "DOCUMENTS" / "Songlengths.md5";
        else if (fs::exists(path / "DOCUMENTS" / "songlengths.md5")) slPath = path / "DOCUMENTS" / "songlengths.md5";
        else if (fs::exists(path / "documents" / "Songlengths.md5")) slPath = path / "documents" / "Songlengths.md5";
        else if (fs::exists(path / "documents" / "songlengths.md5")) slPath = path / "documents" / "songlengths.md5";

        if (!slPath.empty()) {
            try {
                slPath = fs::canonical(slPath);
                LOG_INFO("HVSC Collection detected! Found Songlengths.md5 at: {}", slPath.string());
                SongLengthDB& db = SongLengthDB::getInstance();
                if (db.load(slPath.string())) {
                    m_config.setSonglengthsPath(slPath.string());
                    // Forcer la sauvegarde immédiate de la config
                    fs::path configDir = getConfigDir();
                    m_config.save((configDir / "config.txt").string());
                    LOG_INFO("Songlengths.md5 loaded and config updated.");
                }
            } catch (const std::exception& e) {
                LOG_WARNING("Error resolving Songlengths path: {}", e.what());
            }
        }

        m_playlist.addDirectory(path);
        
        LOG_INFO("Dossier ajouté à la playlist: {}", path.filename().string());
        
        // Lancer l'indexation asynchrone pour ne pas bloquer l'UI (surtout pour HVSC)
        indexPlaylistAsync();

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
            m_database->indexFile(path.string(), ""); // Pas de rootFolder pour un fichier unique
            m_database->save();
            
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
    
#ifdef ENABLE_CLOUD_SAVE
    // Synchronisation finale avant de quitter
    auto& cloudSync = CloudSyncManager::getInstance();
    if (cloudSync.isEnabled()) {
        LOG_INFO("Final cloud synchronization before exit...");
        cloudSync.pushRatings();
        cloudSync.pushHistory();
    }
    // Arrêter le CloudSyncManager explicitement avant le Logger
    cloudSync.shutdown();
#endif

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
            
            // Déterminer le rootFolder : remonter jusqu'au premier enfant direct de m_root
            std::string rootFolder = "";
            PlaylistNode* current = node->parent;
            while (current && current->parent) {
                // Si le parent de current est m_root (parent == nullptr), alors current est le rootFolder
                if (current->parent->parent == nullptr) {
                    // current->parent est m_root, donc current est le rootFolder
                    if (current->isFolder) {
                        rootFolder = current->name;
                    }
                    break;
                }
                current = current->parent;
            }
            
            if (m_database->indexFile(node->filepath, rootFolder)) {
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

#ifdef ENABLE_CLOUD_SAVE
void Application::checkForUpdatesAsync() {
    // Lancer la vérification dans un thread séparé pour ne pas bloquer le démarrage
    std::thread updateThread([this]() {
        
        auto updateInfo = UpdateChecker::checkForUpdate(VERSION_STRING);
        
        {
            std::lock_guard<std::mutex> lock(m_updateState.mutex);
            if (updateInfo.available) {
                m_updateState.available = true;
                m_updateState.showDialog = true;
                m_updateState.version = updateInfo.version;
                m_updateState.tagName = updateInfo.tagName;
                m_updateState.releaseNotes = updateInfo.releaseNotes;
                m_updateState.downloadUrl = updateInfo.downloadUrl;
                m_updateState.error.clear();
                LOG_INFO("Nouvelle version disponible : {} (actuellement : {})", 
                         updateInfo.version, VERSION_STRING);
            } else if (!updateInfo.error.empty()) {
                m_updateState.error = updateInfo.error;
                LOG_DEBUG("Vérification de mise à jour échouée : {}", updateInfo.error);
            } else {
                LOG_DEBUG("Aucune mise à jour disponible (version actuelle : {})", VERSION_STRING);
            }
        }
    });
    updateThread.detach();  // Détacher le thread pour qu'il s'exécute en arrière-plan
}

void Application::startUpdateInstallation() {
    if (m_updateInProgress.load()) {
        return; // Déjà en cours
    }
    
    m_updateInProgress = true;
    
    // Lancer l'installation dans un thread séparé
    m_updateThread = std::thread([this]() {
        std::string downloadUrl;
        std::string version;
        
        {
            std::lock_guard<std::mutex> lock(m_updateState.mutex);
            downloadUrl = m_updateState.downloadUrl;
            version = m_updateState.version;
        }
        
        // Étape 1: Téléchargement
        {
            std::lock_guard<std::mutex> lock(m_updateState.mutex);
            m_updateState.stage = UpdateStage::Downloading;
            m_updateState.progress = 0.1f;
            m_updateState.statusMessage = "Téléchargement de la mise à jour...";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Petit délai pour l'affichage
        
        // Étape 2: Extraction (simulation - UpdateInstaller fait tout)
        {
            std::lock_guard<std::mutex> lock(m_updateState.mutex);
            m_updateState.progress = 0.4f;
            m_updateState.stage = UpdateStage::Extracting;
            m_updateState.statusMessage = "Extraction de l'archive...";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Étape 3: Installation
        {
            std::lock_guard<std::mutex> lock(m_updateState.mutex);
            m_updateState.progress = 0.6f;
            m_updateState.stage = UpdateStage::Installing;
            m_updateState.statusMessage = "Installation de la mise à jour...";
        }
        
        // Obtenir le chemin de l'exécutable AVANT l'installation
        // (car getExecutablePath() pourrait pointer vers .old après renommage)
        std::string exePath = UpdateInstaller::getExecutablePath();
        if (exePath.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_updateState.mutex);
                m_updateState.stage = UpdateStage::Error;
                m_updateState.statusMessage = "Impossible de déterminer le chemin de l'exécutable";
            }
            m_updateInProgress = false;
            return;
        }
        
        // Utiliser UpdateInstaller pour faire tout le travail
        bool success = UpdateInstaller::installUpdate(downloadUrl, version);
        
        {
            std::lock_guard<std::mutex> lock(m_updateState.mutex);
            if (success) {
                m_updateState.stage = UpdateStage::Completed;
                m_updateState.progress = 1.0f;
                m_updateState.statusMessage = "Mise à jour installée avec succès ! Redémarrage...";
            } else {
                m_updateState.stage = UpdateStage::Error;
                m_updateState.statusMessage = "Erreur lors de l'installation";
            }
        }
        
        m_updateInProgress = false;
        
        // Attendre un peu avant de relancer
        if (success) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            // Relancer l'application avec le chemin stocké (pas getExecutablePath() qui pourrait pointer vers .old)
            LOG_INFO("Relance de l'application : {}", exePath);
            if (!exePath.empty()) {
                #ifdef _WIN32
                std::string command = "start \"\" \"" + exePath + "\"";
                int result = system(command.c_str());
                if (result != 0) {
                    LOG_WARNING("Échec du lancement de l'application (code: {})", result);
                }
                #else
                std::string command = "\"" + exePath + "\" &";
                int result = system(command.c_str());
                if (result != 0) {
                    LOG_WARNING("Échec du lancement de l'application (code: {})", result);
                }
                #endif
            }
            
            // Quitter l'application actuelle
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::exit(0);
        }
    });
    m_updateThread.detach();
}

void Application::renderUpdateDialog() {
    // Lire les valeurs nécessaires avec le mutex
    bool showDialog;
    bool userAccepted;
    UpdateStage stage;
    std::string version;
    std::string releaseNotes;
    std::string statusMessage;
    float progress;
    
    {
        std::lock_guard<std::mutex> lock(m_updateState.mutex);
        showDialog = m_updateState.showDialog;
        if (!showDialog) {
            return;
        }
        userAccepted = m_updateState.userAccepted;
        stage = m_updateState.stage;
        version = m_updateState.version;
        releaseNotes = m_updateState.releaseNotes;
        statusMessage = m_updateState.statusMessage;
        progress = m_updateState.progress;
    }
    
    // 1. Déclencher l'ouverture du popup (une seule fois)
    if (!ImGui::IsPopupOpen("Mise à jour disponible")) {
        ImGui::OpenPopup("Mise à jour disponible");
    }
    
    // Centrer la fenêtre quand elle apparaît
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
    
    // 2. Utiliser le vrai composant Modal
    if (ImGui::BeginPopupModal("Mise à jour disponible", NULL, 
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | 
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        
        if (!userAccepted && stage == UpdateStage::None) {
            // Dialog initial : demander confirmation
            ImGui::Text("Une nouvelle version est disponible !");
            ImGui::Spacing();
            ImGui::Text("Version actuelle : %s", VERSION_STRING);
            ImGui::Text("Nouvelle version : %s", version.c_str());
            ImGui::Spacing();
            
            if (!releaseNotes.empty()) {
                if (ImGui::CollapsingHeader("Notes de version")) {
                    ImGui::TextWrapped("%s", releaseNotes.c_str());
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            if (ImGui::Button("Télécharger et installer", ImVec2(-1, 0))) {
                {
                    std::lock_guard<std::mutex> lock(m_updateState.mutex);
                    m_updateState.userAccepted = true;
                }
                startUpdateInstallation();
            }
            ImGui::Spacing();
            if (ImGui::Button("Plus tard", ImVec2(-1, 0))) {
                {
                    std::lock_guard<std::mutex> lock(m_updateState.mutex);
                    m_updateState.showDialog = false;
                }
                ImGui::CloseCurrentPopup();
            }
        } else if (userAccepted) {
            // Afficher la progression
            ImGui::Text("Installation en cours...");
            ImGui::Spacing();
            
            // Afficher l'étape actuelle
            const char* stageText = "";
            switch (stage) {
                case UpdateStage::Downloading:
                    stageText = "Téléchargement";
                    break;
                case UpdateStage::Extracting:
                    stageText = "Extraction";
                    break;
                case UpdateStage::Installing:
                    stageText = "Installation";
                    break;
                case UpdateStage::Completed:
                    stageText = "Terminé";
                    break;
                case UpdateStage::Error:
                    stageText = "Erreur";
                    break;
                default:
                    stageText = "En attente...";
            }
            
            ImGui::Text("Étape : %s", stageText);
            ImGui::Text("%s", statusMessage.c_str());
            ImGui::Spacing();
            
            // Barre de progression
            ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
            
            if (stage == UpdateStage::Error) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Erreur : %s", statusMessage.c_str());
                ImGui::Spacing();
                if (ImGui::Button("Fermer", ImVec2(-1, 0))) {
                    {
                        std::lock_guard<std::mutex> lock(m_updateState.mutex);
                        m_updateState.showDialog = false;
                        m_updateState.userAccepted = false;
                        m_updateState.stage = UpdateStage::None;
                    }
                    ImGui::CloseCurrentPopup();
                }
            } else if (stage == UpdateStage::Completed) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Mise à jour installée avec succès !");
                ImGui::Text("L'application va redémarrer...");
            }
        }
        
        ImGui::EndPopup();
    }
    
    // Mettre à jour showDialog si le popup a été fermé
    if (!showDialog) {
        std::lock_guard<std::mutex> lock(m_updateState.mutex);
        m_updateState.showDialog = false;
    }
}
#endif