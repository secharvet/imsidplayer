#include "imgui_internal.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif
#include "SidPlayer.h"
#include "Config.h"

// ImGui
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL2/SDL.h>

// SDL2_image pour charger des images (PNG, JPEG, etc.)
#if __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#define HAS_SDL2_IMAGE
#endif


namespace fs = std::filesystem;

// Fonction helper pour obtenir le répertoire de configuration (~/.imsidplayer/)
fs::path getConfigDir() {
    fs::path homeDir;
    
#ifdef _WIN32
    // Windows: utiliser APPDATA ou USERPROFILE
    const char* appdata = getenv("APPDATA");
    if (appdata) {
        homeDir = fs::path(appdata);
    } else {
        const char* userprofile = getenv("USERPROFILE");
        if (userprofile) {
            homeDir = fs::path(userprofile);
        }
    }
#else
    // Unix/Linux: utiliser HOME ou getpwuid
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    if (home) {
        homeDir = fs::path(home);
    }
#endif
    
    if (homeDir.empty()) {
        return fs::current_path() / ".imsidplayer";
    }
    
    fs::path configDir = homeDir / ".imsidplayer";
    if (!fs::exists(configDir)) {
        try {
            fs::create_directories(configDir);
        } catch (const std::exception& e) {
            std::cerr << "Impossible de créer le répertoire de configuration: " << e.what() << std::endl;
            return fs::current_path() / ".imsidplayer";
        }
    }
    return configDir;
}

int main(int argc, char* argv[]) {
    // Initialiser SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "Erreur SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    // Charger la configuration depuis ~/.imsidplayer/config.txt
    Config& config = Config::getInstance();
    fs::path configDir = getConfigDir();
    std::string configPath = (configDir / "config.txt").string();
    config.load(configPath);
    
    // Créer la fenêtre avec position et taille sauvegardées
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow(
        "imSid Player",
        config.getWindowX(),
        config.getWindowY(),
        config.getWindowWidth(),
        config.getWindowHeight(),
        window_flags
    );

    if (!window) {
        std::cerr << "Erreur: Impossible de créer la fenêtre SDL" << std::endl;
        SDL_Quit();
        return -1;
    }

    // Créer le renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED
    );

    if (!renderer) {
        std::cerr << "Erreur: Impossible de créer le renderer SDL" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Initialiser ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Charger une police plus grande
    float fontSize = 18.0f;
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = 1.2f; // Augmenter la taille globale de la police

    // Style ImGui
    ImGui::StyleColorsDark();
    
    // Ajuster le style pour des boutons plus grands
    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(15.0f, 10.0f); // Boutons plus grands
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.WindowPadding = ImVec2(20.0f, 20.0f); // Padding plus important pour éviter que le contenu soit collé
    style.FrameRounding = 5.0f;
    
    // Rendre ImGui transparent pour voir l'image de fond
    ImVec4* colors = style.Colors;
    // Réduire l'alpha des couleurs de fond pour la transparence
    colors[ImGuiCol_WindowBg].w = 0.0f; // Fenêtre complètement transparente
    colors[ImGuiCol_ChildBg].w = 0.0f; // Enfants transparents
    colors[ImGuiCol_PopupBg].w = 0.8f; // Popups légèrement opaques pour lisibilité
    colors[ImGuiCol_FrameBg].w = 0.3f; // Frames semi-transparents
    colors[ImGuiCol_FrameBgHovered].w = 0.4f;
    colors[ImGuiCol_FrameBgActive].w = 0.5f;
    colors[ImGuiCol_Button].w = 0.4f; // Boutons semi-transparents
    colors[ImGuiCol_ButtonHovered].w = 0.6f;
    colors[ImGuiCol_ButtonActive].w = 0.7f;
    colors[ImGuiCol_Tab].w = 0.3f; // Onglets semi-transparents
    colors[ImGuiCol_TabHovered].w = 0.5f;
    colors[ImGuiCol_TabActive].w = 0.6f;
    colors[ImGuiCol_Header].w = 0.4f;
    colors[ImGuiCol_HeaderHovered].w = 0.5f;
    colors[ImGuiCol_HeaderActive].w = 0.6f;

    // Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // Créer le player
    SidPlayer player;
    
    // Images de fond (optionnelles)
    struct BackgroundImage {
        SDL_Texture* texture = nullptr;
        std::string filename;
        int width = 0;
        int height = 0;
    };
    std::vector<BackgroundImage> backgroundImages;
    int currentBackgroundIndex = config.getBackgroundIndex();
    bool showBackground = config.isBackgroundShown();
    int backgroundAlpha = config.getBackgroundAlpha(); // Transparence de l'image de fond (0-255)
    
    // Variable pour savoir si on est dans l'onglet Config (pour drag and drop d'images)
    bool isConfigTabActive = false;
    
    // Fonction pour recharger les images de fond
    auto reloadBackgroundImages = [&]() {
        // Libérer les textures existantes
        for (auto& bgImg : backgroundImages) {
            if (bgImg.texture) {
                SDL_DestroyTexture(bgImg.texture);
            }
        }
        backgroundImages.clear();
        
#ifdef HAS_SDL2_IMAGE
        fs::path configDir = getConfigDir();
        fs::path backgroundPath = configDir / "background";
        
        if (!fs::exists(backgroundPath)) {
            try {
                fs::create_directories(backgroundPath);
            } catch (const std::exception& e) {
                std::cerr << "Impossible de créer le répertoire background: " << e.what() << std::endl;
                return;
            }
        }
        
        std::vector<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".gif"};
        try {
            for (const auto& entry : fs::directory_iterator(backgroundPath)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    
                    if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) != imageExtensions.end()) {
                        std::string filename = entry.path().filename().string();
                        std::string fullPath = entry.path().string();
                        if (filename[0] != '.') {
                            SDL_Surface* bgSurface = IMG_Load(fullPath.c_str());
                            if (bgSurface) {
                                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, bgSurface);
                                if (texture) {
                                    BackgroundImage bgImg;
                                    bgImg.texture = texture;
                                    bgImg.filename = filename;
                                    bgImg.width = bgSurface->w;
                                    bgImg.height = bgSurface->h;
                                    backgroundImages.push_back(bgImg);
                                }
                                SDL_FreeSurface(bgSurface);
                            }
                        }
                    }
                }
            }
            
            // Réajuster l'index si nécessaire
            if (!backgroundImages.empty()) {
                if (currentBackgroundIndex >= (int)backgroundImages.size()) {
                    currentBackgroundIndex = 0;
                }
            } else {
                currentBackgroundIndex = -1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Erreur lors du rechargement des images: " << e.what() << std::endl;
        }
#endif
    };
    
#ifdef HAS_SDL2_IMAGE
    // Initialiser SDL_image
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cerr << "Erreur SDL_image: " << IMG_GetError() << std::endl;
        std::cerr << "Les images de fond seront désactivées" << std::endl;
    } else {
        std::cout << "SDL_image initialisé avec succès" << std::endl;
        
        // Chercher toutes les images de fond dans ~/.imsidplayer/background
        fs::path configDir = getConfigDir();
        fs::path backgroundPath = configDir / "background";
        
        // Créer le répertoire s'il n'existe pas
        if (!fs::exists(backgroundPath)) {
            try {
                fs::create_directories(backgroundPath);
                std::cout << "Répertoire background créé: " << backgroundPath << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Impossible de créer le répertoire background: " << e.what() << std::endl;
            }
        }
        
        std::cout << "Recherche d'images dans: " << backgroundPath << std::endl;
        
        std::vector<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".gif"};
        int imagesFound = 0;
        try {
            for (const auto& entry : fs::directory_iterator(backgroundPath)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    
                    // Vérifier si c'est une image
                    if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) != imageExtensions.end()) {
                        std::string filename = entry.path().filename().string();
                        std::string fullPath = entry.path().string();
                        // Ignorer les fichiers qui commencent par "." (fichiers cachés)
                        if (filename[0] != '.') {
                            imagesFound++;
                            SDL_Surface* bgSurface = IMG_Load(fullPath.c_str());
                            if (bgSurface) {
                                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, bgSurface);
                                if (texture) {
                                    BackgroundImage bgImg;
                                    bgImg.texture = texture;
                                    bgImg.filename = filename;
                                    bgImg.width = bgSurface->w;
                                    bgImg.height = bgSurface->h;
                                    backgroundImages.push_back(bgImg);
                                    std::cout << "Image de fond chargée: " << filename << " (" << bgImg.width << "x" << bgImg.height << ")" << std::endl;
                                } else {
                                    std::cerr << "Erreur création texture pour: " << filename << " - " << SDL_GetError() << std::endl;
                                }
                                SDL_FreeSurface(bgSurface);
                            } else {
                                std::cerr << "Erreur chargement image: " << filename << " - " << IMG_GetError() << std::endl;
                            }
                        }
                    }
                }
            }
            
            if (!backgroundImages.empty()) {
                // Restaurer le background par nom si disponible
                if (!config.getBackgroundFilename().empty()) {
                    bool found = false;
                    for (size_t i = 0; i < backgroundImages.size(); i++) {
                        if (backgroundImages[i].filename == config.getBackgroundFilename()) {
                            currentBackgroundIndex = i;
                            found = true;
                            break;
                        }
                    }
                    if (!found && config.getBackgroundIndex() >= 0 && config.getBackgroundIndex() < (int)backgroundImages.size()) {
                        currentBackgroundIndex = config.getBackgroundIndex();
                    } else if (!found) {
                        currentBackgroundIndex = 0;
                    }
                } else if (config.getBackgroundIndex() >= 0 && config.getBackgroundIndex() < (int)backgroundImages.size()) {
                    currentBackgroundIndex = config.getBackgroundIndex();
                } else {
                    currentBackgroundIndex = 0;
                }
                showBackground = config.isBackgroundShown();
                std::cout << "Total images de fond chargées: " << backgroundImages.size() << " / " << imagesFound << " trouvées" << std::endl;
            } else if (imagesFound > 0) {
                std::cerr << "Aucune image n'a pu être chargée sur " << imagesFound << " trouvées" << std::endl;
            } else {
                std::cout << "Aucune image trouvée dans " << backgroundPath << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Erreur lors de la recherche d'images dans " << backgroundPath << ": " << e.what() << std::endl;
        }
    }
#else
    std::cout << "SDL2_image non disponible. Installez libsdl2-image-dev pour activer les images de fond." << std::endl;
#endif

    // Variables pour l'interface
    std::string selectedFile;
    bool showFileDialog = false;
    fs::path currentDirectory = fs::current_path();
    std::string selectedFilePath = "";
    std::vector<std::string> directoryEntries;
    std::string filterExtension = ".sid";
    
    // Playlist avec structure arborescente
    struct PlaylistNode {
        std::string name;
        std::string filepath;  // Vide pour les dossiers
        bool isFolder;
        std::vector<std::unique_ptr<PlaylistNode>> children;
        PlaylistNode* parent = nullptr;
        
        PlaylistNode(const std::string& n, const std::string& path = "", bool folder = false) 
            : name(n), filepath(path), isFolder(folder) {}
    };
    
    std::unique_ptr<PlaylistNode> playlistRoot = std::make_unique<PlaylistNode>("Playlist", "", true);
    PlaylistNode* currentPlaylistNode = nullptr;
    
    // Restaurer la playlist depuis la config (lambda récursive avec std::function)
    std::function<std::unique_ptr<PlaylistNode>(const Config::PlaylistItem&, PlaylistNode*)> convertPlaylistItem;
    convertPlaylistItem = [&](const Config::PlaylistItem& item, PlaylistNode* parent) -> std::unique_ptr<PlaylistNode> {
        auto node = std::make_unique<PlaylistNode>(item.name, item.path, item.isFolder);
        node->parent = parent;
        for (const auto& child : item.children) {
            node->children.push_back(convertPlaylistItem(child, node.get()));
        }
        return node;
    };
    // Restaurer la playlist depuis la config
    const auto& loadedPlaylist = config.getPlaylist();
    std::cout << "Chargement de la playlist: " << loadedPlaylist.size() << " items trouvés" << std::endl;
    for (const auto& item : loadedPlaylist) {
        playlistRoot->children.push_back(convertPlaylistItem(item, playlistRoot.get()));
    }
    std::cout << "Playlist restaurée: " << playlistRoot->children.size() << " items chargés" << std::endl;
    
    // Restaurer le fichier en cours si disponible
    if (!config.getCurrentFile().empty() && fs::exists(config.getCurrentFile())) {
        player.loadFile(config.getCurrentFile());
    }
    
    // Restore voice states
    for (int i = 0; i < 3; i++) {
        player.setVoiceMute(i, !config.isVoiceActive(i));
    }
    
    // Restaurer l'engine audio
    
    // Fonction pour mettre à jour la liste des fichiers
    auto updateDirectoryList = [&]() {
        directoryEntries.clear();
        try {
            for (const auto& entry : fs::directory_iterator(currentDirectory)) {
                if (entry.is_directory()) {
                    directoryEntries.push_back("[DIR] " + entry.path().filename().string());
                } else {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == filterExtension || filterExtension.empty()) {
                        directoryEntries.push_back(entry.path().filename().string());
                    }
                }
            }
            std::sort(directoryEntries.begin(), directoryEntries.end());
        } catch (const std::exception& e) {
            std::cerr << "Erreur lecture répertoire: " << e.what() << std::endl;
        }
    };
    
    updateDirectoryList();

    // Boucle principale
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_DROPFILE) {
                // Gérer le drag & drop de fichiers et dossiers
                char* droppedPath = event.drop.file;
                if (droppedPath) {
                    fs::path path(droppedPath);
                    
                    // Vérifier d'abord si c'est une image (fonctionne sur tous les onglets)
                    if (fs::is_regular_file(path)) {
                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        std::vector<std::string> imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".gif"};
                        
                        if (std::find(imageExtensions.begin(), imageExtensions.end(), ext) != imageExtensions.end()) {
                            // C'est une image, la copier dans le dossier background
                            try {
                                fs::path configDir = getConfigDir();
                                fs::path backgroundPath = configDir / "background";
                                
                                if (!fs::exists(backgroundPath)) {
                                    fs::create_directories(backgroundPath);
                                }
                                
                                fs::path destPath = backgroundPath / path.filename();
                                
                                // Si le fichier existe déjà, ajouter un numéro
                                int counter = 1;
                                std::string baseName = path.stem().string();
                                std::string extension = path.extension().string();
                                while (fs::exists(destPath)) {
                                    std::string newName = baseName + "_" + std::to_string(counter) + extension;
                                    destPath = backgroundPath / newName;
                                    counter++;
                                }
                                
                                fs::copy_file(path, destPath, fs::copy_options::overwrite_existing);
                                std::cout << "Image copiée dans background: " << destPath.filename().string() << std::endl;
                                
                                // Recharger les images
                                reloadBackgroundImages();
                                
                                // Sélectionner la nouvelle image
                                if (!backgroundImages.empty()) {
                                    for (size_t i = 0; i < backgroundImages.size(); i++) {
                                        if (backgroundImages[i].filename == destPath.filename().string()) {
                                            currentBackgroundIndex = i;
                                            break;
                                        }
                                    }
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "Erreur lors de la copie de l'image: " << e.what() << std::endl;
                            }
                            
                            SDL_free(droppedPath);
                            continue; // Ne pas traiter comme un fichier SID
                        }
                    }
                    
                    // Fonction pour trier les enfants d'un nœud (récursive)
                    std::function<void(PlaylistNode*)> sortNode;
                    sortNode = [&sortNode](PlaylistNode* node) {
                        std::sort(node->children.begin(), node->children.end(),
                            [](const std::unique_ptr<PlaylistNode>& a, const std::unique_ptr<PlaylistNode>& b) {
                                // D'abord les dossiers, puis les fichiers, tous triés alphabétiquement
                                if (a->isFolder != b->isFolder) {
                                    return a->isFolder > b->isFolder; // Dossiers avant fichiers
                                }
                                return a->name < b->name; // Tri alphabétique
                            });
                        // Trier récursivement les sous-dossiers
                        for (auto& child : node->children) {
                            if (child->isFolder) {
                                sortNode(child.get());
                            }
                        }
                    };
                    
                    if (fs::is_directory(path)) {
                        // C'est un dossier : créer d'abord un nœud pour le dossier racine
                        auto rootFolderNode = std::make_unique<PlaylistNode>(
                            path.filename().string(), "", true);
                        rootFolderNode->parent = playlistRoot.get();
                        PlaylistNode* rootFolderPtr = rootFolderNode.get();
                        
                        // Fonction récursive pour ajouter le contenu du dossier
                        std::function<void(const fs::path&, PlaylistNode*)> addDirectory = 
                            [&](const fs::path& dir, PlaylistNode* parent) {
                                try {
                                    for (const auto& entry : fs::directory_iterator(dir)) {
                                        if (entry.is_directory()) {
                                            // Créer un nœud dossier
                                            auto folderNode = std::make_unique<PlaylistNode>(
                                                entry.path().filename().string(), "", true);
                                            folderNode->parent = parent;
                                            PlaylistNode* folderPtr = folderNode.get();
                                            parent->children.push_back(std::move(folderNode));
                                            addDirectory(entry.path(), folderPtr);
                                        } else if (entry.is_regular_file()) {
                                            std::string ext = entry.path().extension().string();
                                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                            if (ext == ".sid") {
                                                auto fileNode = std::make_unique<PlaylistNode>(
                                                    entry.path().filename().string(), 
                                                    entry.path().string(), 
                                                    false);
                                                fileNode->parent = parent;
                                                parent->children.push_back(std::move(fileNode));
                                            }
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    std::cerr << "Erreur lecture dossier: " << e.what() << std::endl;
                                }
                            };
                        
                        // Parcourir récursivement le contenu du dossier
                        addDirectory(path, rootFolderPtr);
                        
                        // Ajouter le nœud racine à la playlist
                        playlistRoot->children.push_back(std::move(rootFolderNode));
                        sortNode(playlistRoot.get());
                        std::cout << "Dossier ajouté à la playlist: " << path.filename().string() << std::endl;
                    } else if (fs::is_regular_file(path)) {
                        // C'est un fichier
                        std::string ext = path.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".sid") {
                            auto fileNode = std::make_unique<PlaylistNode>(
                                path.filename().string(), 
                                path.string(), 
                                false);
                            fileNode->parent = playlistRoot.get();
                            playlistRoot->children.push_back(std::move(fileNode));
                            sortNode(playlistRoot.get());
                            std::cout << "Fichier ajouté à la playlist: " << path.filename().string() << std::endl;
                        }
                    }
                    SDL_free(droppedPath);
                }
            }
        }

        // Réinitialiser isConfigTabActive au début de chaque frame
        isConfigTabActive = false;
        
        // Démarrer le frame ImGui
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Interface utilisateur principale - layout avec deux panneaux
        {
            int windowWidth, windowHeight;
            SDL_GetWindowSize(window, &windowWidth, &windowHeight);
            
            // Panneau principal (gauche) - 70% de la largeur
            float mainPanelWidth = windowWidth * 0.7f;
            float playlistPanelWidth = windowWidth - mainPanelWidth;
            
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(mainPanelWidth, (float)windowHeight), ImGuiCond_Always);
            // Rendre la fenêtre transparente pour voir l'image de fond
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::Begin("imSid Player", nullptr, 
                ImGuiWindowFlags_NoTitleBar | 
                ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoMove | 
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus);

            // Créer les onglets
            if (ImGui::BeginTabBar("MainTabs")) {
                // Onglet Player
                if (ImGui::BeginTabItem("Player")) {
                    // Section informations
                    ImGui::Text("Current file:");
                    if (!player.getCurrentFile().empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", 
                            fs::path(player.getCurrentFile()).filename().string().c_str());
                        ImGui::Separator();
                        ImGui::TextWrapped("Information: %s", player.getTuneInfo().c_str());
                    } else {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No file loaded");
                    }
                    
                    ImGui::Separator();
                    ImGui::Spacing();

            // Contrôles de lecture (boutons plus grands)
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 12.0f));
            
            if (player.getCurrentFile().empty()) {
                ImGui::BeginDisabled();
            }

            // Boutons de contrôle sur la même ligne
            float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
            
            if (player.isPlaying() && !player.isPaused()) {
                if (ImGui::Button("|| Pause", ImVec2(buttonWidth, 0))) {
                    player.pause();
                }
            } else {
                if (ImGui::Button("> Play", ImVec2(buttonWidth, 0))) {
                    player.play();
                }
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("[] Stop", ImVec2(buttonWidth, 0))) {
                player.stop();
            }

            if (player.getCurrentFile().empty()) {
                ImGui::EndDisabled();
            }
            
            ImGui::PopStyleVar();

            ImGui::Spacing();
            ImGui::Separator();

            // State
            ImGui::Text("State:");
            ImGui::SameLine();
            if (player.isPlaying()) {
                if (player.isPaused()) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Paused");
                } else {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Playing");
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stopped");
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            
            ImGui::Spacing();
            ImGui::Separator();
            
            // Voice controls (inverted logic: checkbox = voice active)
            if (!player.getCurrentFile().empty()) {
                ImGui::Text("Voices:");
                ImGui::Spacing();
                
                // Inverser la logique : checkbox cochée = voix active (non mutée)
                bool voice0Active = !player.isVoiceMuted(0);
                bool voice1Active = !player.isVoiceMuted(1);
                bool voice2Active = !player.isVoiceMuted(2);
                
                // Sauvegarder l'état avant le checkbox pour détecter le changement
                bool prev0 = voice0Active;
                bool prev1 = voice1Active;
                bool prev2 = voice2Active;
                
                ImGui::Checkbox("Voice 1", &voice0Active);
                ImGui::SameLine();
                ImGui::Checkbox("Voice 2", &voice1Active);
                ImGui::SameLine();
                ImGui::Checkbox("Voice 3", &voice2Active);
                
                // Détecter les changements et appeler setVoiceMute
                if (voice0Active != prev0) {
                    player.setVoiceMute(0, !voice0Active);
                }
                if (voice1Active != prev1) {
                    player.setVoiceMute(1, !voice1Active);
                }
                if (voice2Active != prev2) {
                    player.setVoiceMute(2, !voice2Active);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            
                    // Oscilloscopes par voix (approche bas niveau optimisée)
                    if (!player.getCurrentFile().empty()) {
                        ImGui::Text("Oscilloscopes by voice:");
                        ImGui::Spacing();
                        
                        // Accès direct aux buffers (pas de copies!)
                        const float* voice0 = player.getVoiceBuffer(0);
                        const float* voice1 = player.getVoiceBuffer(1);
                        const float* voice2 = player.getVoiceBuffer(2);
                        
                        // Utiliser ImGui::PlotLines qui est optimisé en interne
                        float plotHeight = 120.0f;
                        float plotWidth = ImGui::GetContentRegionAvail().x / 3.0f - 5.0f;
                        
                        // Voix 0 (Rouge)
                        ImVec2 plotPos0 = ImGui::GetCursorScreenPos();
                        
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                        ImGui::PlotLines("##voice0", voice0, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                                        ImVec2(plotWidth, plotHeight));
                        ImGui::PopItemFlag();
                        ImGui::PopStyleColor();
                        // Afficher "0" à l'intérieur en haut à gauche
                        ImGui::GetWindowDrawList()->AddText(ImVec2(plotPos0.x + 5.0f, plotPos0.y + 5.0f), 
                                                           IM_COL32(255, 255, 255, 255), "0");
                        
                        ImGui::SameLine();
                        
                        // Voix 1 (Vert)
                        ImVec2 plotPos1 = ImGui::GetCursorScreenPos();
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                        ImGui::PlotLines("##voice1", voice1, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                                        ImVec2(plotWidth, plotHeight));
                        ImGui::PopItemFlag();
 
                        ImGui::PopStyleColor();
                        // Afficher "1" à l'intérieur en haut à gauche
                        ImGui::GetWindowDrawList()->AddText(ImVec2(plotPos1.x + 5.0f, plotPos1.y + 5.0f), 
                                                           IM_COL32(255, 255, 255, 255), "1");
                        
                        ImGui::SameLine();
                        
                        // Voix 2 (Bleu)
                        ImVec2 plotPos2 = ImGui::GetCursorScreenPos();
                        
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.3f, 1.0f, 1.0f));
                        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                       
                        ImGui::PlotLines("##voice2", voice2, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                                        ImVec2(plotWidth, plotHeight));
                        ImGui::PopItemFlag();
                        ImGui::PopStyleColor();
                       
                        // Afficher "2" à l'intérieur en haut à gauche
                        ImGui::GetWindowDrawList()->AddText(ImVec2(plotPos2.x + 5.0f, plotPos2.y + 5.0f), 
                                                           IM_COL32(255, 255, 255, 255), "2");
                    }
                    
                    ImGui::EndTabItem();
                }
                
                // Onglet Config
                if (ImGui::BeginTabItem("Config")) {
                    isConfigTabActive = true;
                    ImGui::Text("Configuration");
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    // Background image section
                    ImGui::Text("Background image");
                    ImGui::Separator();
                    
                    // Zone de drag and drop pour les images
                    ImGui::Text("Drag & drop image files here to add them");
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 0.3f));
                    ImGui::BeginChild("DropZone", ImVec2(-1, 60), true);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                        "Drop PNG, JPG, JPEG, BMP, GIF files here");
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                    
                    if (backgroundImages.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.5f, 1.0f), "Aucune image trouvée dans background/");
#ifdef HAS_SDL2_IMAGE
#else
                        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.5f, 1.0f), "SDL2_image non disponible");
#endif
                    } else {
                        ImGui::Text("Current image:");
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                            "%d/%d - %s", 
                            currentBackgroundIndex + 1, 
                            (int)backgroundImages.size(),
                            backgroundImages[currentBackgroundIndex].filename.c_str());
                        
                        ImGui::Spacing();
                        
                        // Boutons de navigation
                        if (ImGui::Button("<", ImVec2(50, 0))) {
                            currentBackgroundIndex = (currentBackgroundIndex - 1 + backgroundImages.size()) % backgroundImages.size();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(">", ImVec2(50, 0))) {
                            currentBackgroundIndex = (currentBackgroundIndex + 1) % backgroundImages.size();
                        }
                        
                        ImGui::Spacing();
                        
                        // Bouton Afficher/Masquer
                        if (ImGui::Button(showBackground ? "Hide" : "Show", ImVec2(120, 0))) {
                            showBackground = !showBackground;
                        }
                        
                        ImGui::Spacing();
                        ImGui::Separator();
                        
                        // Slider de transparence
                        ImGui::Text("Transparency:");
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%d%%", 
                            (int)((backgroundAlpha / 255.0f) * 100.0f));
                        if (ImGui::SliderInt("##alpha", &backgroundAlpha, 0, 255, "%d")) {
                            // L'alpha est mis à jour automatiquement
                        }
                    }
                    
                    ImGui::Spacing();
                    
                    ImGui::EndTabItem();
                } else {
                    isConfigTabActive = false;
                }
                
                ImGui::EndTabBar();
            }

            ImGui::End();
        }
        
        // Panneau Playlist (droite)
        {
            int windowWidth, windowHeight;
            SDL_GetWindowSize(window, &windowWidth, &windowHeight);
            float mainPanelWidth = windowWidth * 0.7f;
            float playlistPanelWidth = windowWidth - mainPanelWidth;
            
            ImGui::SetNextWindowPos(ImVec2(mainPanelWidth, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(playlistPanelWidth, (float)windowHeight), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f); // Playlist transparente
            ImGui::Begin("Playlist", nullptr, 
                ImGuiWindowFlags_NoTitleBar | 
                ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoMove | 
                ImGuiWindowFlags_NoCollapse);
            
            // Zone de drop pour les fichiers et dossiers
            ImGui::Text("Drag & drop .sid files or folders here");
            ImGui::Separator();
            
            // Clear playlist button
            if (ImGui::Button("Clear", ImVec2(-1, 0))) {
                playlistRoot->children.clear();
                currentPlaylistNode = nullptr;
            }
            
            ImGui::Separator();
            ImGui::Spacing();
            
            // Tree view de la playlist
            ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
            
            // Fonction récursive pour afficher la tree view
            std::function<void(PlaylistNode*, int)> renderNode = [&](PlaylistNode* node, int depth) {
                if (!node) return;
                
                ImGui::PushID(node);
                
                bool isCurrent = (currentPlaylistNode == node && !node->filepath.empty());
                bool isSelected = (currentPlaylistNode == node);
                
                if (node->isFolder) {
                    // Afficher un dossier avec TreeNode
                    bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), 
                        ImGuiTreeNodeFlags_OpenOnArrow | 
                        (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
                    
                    // Clic sur le dossier
                    if (ImGui::IsItemClicked()) {
                        // Ne rien faire pour les dossiers
                    }
                    
                    if (nodeOpen) {
                        // Afficher les enfants (triés)
                        for (auto& child : node->children) {
                            renderNode(child.get(), depth + 1);
                        }
                        ImGui::TreePop();
                    }
                } else {
                    // Afficher un fichier avec indentation réduite
                    // Ajouter de l'indentation pour les fichiers dans des dossiers
                    float indentAmount = depth * 10.0f; // 10 pixels par niveau (réduit)
                    if (depth > 0) {
                        ImGui::Indent(indentAmount);
                    }
                    
                    if (isCurrent) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                    }
                    
                    // Utiliser Selectable pour les fichiers
                    if (ImGui::Selectable(node->name.c_str(), isSelected)) {
                        currentPlaylistNode = node;
                        if (!node->filepath.empty() && player.loadFile(node->filepath)) {
                            player.play();
                        }
                    }
                    
                    if (isCurrent) {
                        ImGui::PopStyleColor();
                    }
                    
                    if (depth > 0) {
                        ImGui::Unindent(indentAmount);
                    }
                }
                
                ImGui::PopID();
            };
            
            // Afficher tous les enfants de la racine (triés)
            for (auto& child : playlistRoot->children) {
                renderNode(child.get(), 0);
            }
            
            ImGui::EndChild();
            
            // Contrôles de navigation
            ImGui::Spacing();
            
            // Fonction pour collecter tous les fichiers de la playlist
            std::vector<PlaylistNode*> allFiles;
            std::function<void(PlaylistNode*)> collectFiles = [&](PlaylistNode* node) {
                if (!node) return;
                if (!node->isFolder && !node->filepath.empty()) {
                    allFiles.push_back(node);
                }
                for (auto& child : node->children) {
                    collectFiles(child.get());
                }
            };
            allFiles.clear();
            collectFiles(playlistRoot.get());
            
            // Boutons Précédent et Suivant sur la même ligne
            float navButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
            
            if (ImGui::Button("<< Previous", ImVec2(navButtonWidth, 0))) {
                if (!allFiles.empty() && currentPlaylistNode) {
                    auto it = std::find(allFiles.begin(), allFiles.end(), currentPlaylistNode);
                    if (it != allFiles.end()) {
                        if (it == allFiles.begin()) {
                            it = allFiles.end() - 1;
                        } else {
                            --it;
                        }
                        currentPlaylistNode = *it;
                        if (player.loadFile(currentPlaylistNode->filepath)) {
                            player.play();
                        }
                    } else if (!allFiles.empty()) {
                        currentPlaylistNode = allFiles.back();
                        if (player.loadFile(currentPlaylistNode->filepath)) {
                            player.play();
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(">> Next", ImVec2(navButtonWidth, 0))) {
                if (!allFiles.empty() && currentPlaylistNode) {
                    auto it = std::find(allFiles.begin(), allFiles.end(), currentPlaylistNode);
                    if (it != allFiles.end()) {
                        ++it;
                        if (it == allFiles.end()) {
                            it = allFiles.begin();
                        }
                        currentPlaylistNode = *it;
                        if (player.loadFile(currentPlaylistNode->filepath)) {
                            player.play();
                        }
                    } else if (!allFiles.empty()) {
                        currentPlaylistNode = allFiles[0];
                        if (player.loadFile(currentPlaylistNode->filepath)) {
                            player.play();
                        }
                    }
                }
            }
            
            ImGui::End();
        }
        
        // Dialog de sélection de fichier (File Browser)
        if (showFileDialog) {
            ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.9f); // Dialog légèrement opaque pour lisibilité
            ImGui::Begin("Select a SID file", &showFileDialog, ImGuiWindowFlags_None);
            
            // Navigation bar (current directory)
            ImGui::Text("Directory:");
            ImGui::SameLine();
            char dirBuffer[512];
            strncpy(dirBuffer, currentDirectory.string().c_str(), sizeof(dirBuffer) - 1);
            dirBuffer[sizeof(dirBuffer) - 1] = '\0';
            
            if (ImGui::InputText("##dir", dirBuffer, sizeof(dirBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                fs::path newPath(dirBuffer);
                if (fs::exists(newPath) && fs::is_directory(newPath)) {
                    currentDirectory = newPath;
                    updateDirectoryList();
                }
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Actualiser")) {
                updateDirectoryList();
            }
            
            ImGui::Separator();
            
            // Boutons de navigation
            if (ImGui::Button("< Previous")) {
                if (currentDirectory.has_parent_path() && currentDirectory != currentDirectory.root_path()) {
                    currentDirectory = currentDirectory.parent_path();
                    updateDirectoryList();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Accueil")) {
#ifdef _WIN32
                const char* home = getenv("USERPROFILE");
                if (!home) home = getenv("APPDATA");
                currentDirectory = fs::path(home ? home : "C:\\");
#else
                const char* home = getenv("HOME");
                currentDirectory = fs::path(home ? home : "/");
#endif
                updateDirectoryList();
            }
            ImGui::SameLine();
            if (ImGui::Button("Racine")) {
                currentDirectory = currentDirectory.root_path();
                updateDirectoryList();
            }
            
            ImGui::Separator();
            
            // Liste des fichiers et dossiers
            ImGui::BeginChild("FileList", ImVec2(0, -80), true, ImGuiWindowFlags_HorizontalScrollbar);
            
            for (size_t i = 0; i < directoryEntries.size(); ++i) {
                const std::string& entry = directoryEntries[i];
                bool isDirectory = entry.find("[DIR]") == 0;
                std::string displayName = isDirectory ? entry.substr(6) : entry;
                
                if (isDirectory) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[DIR] %s", displayName.c_str());
                } else {
                    ImGui::Text("      %s", displayName.c_str());
                }
                
                if (ImGui::IsItemClicked()) {
                    if (isDirectory) {
                        currentDirectory /= displayName;
                        updateDirectoryList();
                    } else {
                        selectedFilePath = (currentDirectory / displayName).string();
                    }
                }
                
                // Double-clic pour ouvrir
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    if (isDirectory) {
                        currentDirectory /= displayName;
                        updateDirectoryList();
                    } else {
                        selectedFilePath = (currentDirectory / displayName).string();
                        if (player.loadFile(selectedFilePath)) {
                            showFileDialog = false;
                            selectedFilePath = "";
                        }
                    }
                }
            }
            
            ImGui::EndChild();
            
            ImGui::Separator();
            
            // Fichier sélectionné
            ImGui::Text("Fichier sélectionné:");
            ImGui::SameLine();
            if (selectedFilePath.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Aucun");
            } else {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", 
                    fs::path(selectedFilePath).filename().string().c_str());
            }
            
            ImGui::Spacing();
            
            // Boutons d'action
            if (ImGui::Button("Load", ImVec2(150, 0))) {
                if (!selectedFilePath.empty() && fs::exists(selectedFilePath)) {
                    if (player.loadFile(selectedFilePath)) {
                        showFileDialog = false;
                        selectedFilePath = "";
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(150, 0))) {
                showFileDialog = false;
                selectedFilePath = "";
            }
            
            ImGui::End();
        }

        // Rendu
        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        
        // Effacer le renderer d'abord
        ImVec4 clearColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        SDL_SetRenderDrawColor(renderer, 
            (Uint8)(clearColor.x * 255), 
            (Uint8)(clearColor.y * 255), 
            (Uint8)(clearColor.z * 255), 
            (Uint8)(clearColor.w * 255));
        SDL_RenderClear(renderer);
        
        // Afficher l'image de fond si disponible (AVANT ImGui pour qu'elle soit derrière)
        if (showBackground && !backgroundImages.empty() && currentBackgroundIndex >= 0 && 
            currentBackgroundIndex < (int)backgroundImages.size()) {
            SDL_Texture* currentBg = backgroundImages[currentBackgroundIndex].texture;
            if (currentBg) {
                int windowWidth, windowHeight;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                
                // Calculer les dimensions pour couvrir toute la fenêtre
                SDL_Rect destRect = {0, 0, windowWidth, windowHeight};
                
                // Si alpha = 255 (100%), utiliser BLENDMODE_NONE pour pas de transparence
                // Sinon utiliser BLENDMODE_BLEND pour la transparence
                if (backgroundAlpha >= 255) {
                    SDL_SetTextureBlendMode(currentBg, SDL_BLENDMODE_NONE);
                    SDL_SetTextureAlphaMod(currentBg, 255);
                } else {
                    SDL_SetTextureBlendMode(currentBg, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureAlphaMod(currentBg, backgroundAlpha);
                }
                SDL_RenderCopy(renderer, currentBg, nullptr, &destRect);
            }
        }
        
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // Sauvegarder la configuration avant de quitter
    // Convertir PlaylistNode vers Config::PlaylistItem (lambda récursive avec std::function)
    std::function<Config::PlaylistItem(const PlaylistNode*)> convertToConfigItem;
    convertToConfigItem = [&](const PlaylistNode* node) -> Config::PlaylistItem {
        Config::PlaylistItem item;
        item.name = node->name;
        item.path = node->filepath;
        item.isFolder = node->isFolder;
        for (const auto& child : node->children) {
            item.children.push_back(convertToConfigItem(child.get()));
        }
        return item;
    };
    std::vector<Config::PlaylistItem> configPlaylist;
    for (const auto& child : playlistRoot->children) {
        configPlaylist.push_back(convertToConfigItem(child.get()));
    }
    config.setPlaylist(configPlaylist);
    
    // Mettre à jour les valeurs de config
    config.setCurrentFile(player.getCurrentFile());
    config.setBackgroundIndex(currentBackgroundIndex);
    if (!backgroundImages.empty() && currentBackgroundIndex >= 0 && currentBackgroundIndex < (int)backgroundImages.size()) {
        config.setBackgroundFilename(backgroundImages[currentBackgroundIndex].filename);
    } else {
        config.setBackgroundFilename("");
    }
    config.setBackgroundShown(showBackground);
    config.setBackgroundAlpha(backgroundAlpha);
    
    // Sauvegarder la position et taille de la fenêtre
    int winX, winY, winW, winH;
    SDL_GetWindowPosition(window, &winX, &winY);
    SDL_GetWindowSize(window, &winW, &winH);
    config.setWindowPos(winX, winY);
    config.setWindowSize(winW, winH);
    
    // Save voice states
    for (int i = 0; i < 3; i++) {
        config.setVoiceActive(i, !player.isVoiceMuted(i));
    }
    
    // Sauvegarder l'engine audio
    
    // Sauvegarder dans le fichier
    config.save(configPath);
    
    // Nettoyage
    for (auto& bgImg : backgroundImages) {
        if (bgImg.texture) {
            SDL_DestroyTexture(bgImg.texture);
        }
    }
    
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    
#ifdef HAS_SDL2_IMAGE
    IMG_Quit();
#endif
    
    SDL_Quit();

    return 0;
}

