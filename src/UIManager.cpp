#include "UIManager.h"
#include "SongLengthDB.h"
#include "Config.h"
#include "Utils.h"
#include "FilterWidget.h"
#include "IconsFontAwesome6.h"
#include "Logger.h"
#ifdef ENABLE_CLOUD_SAVE
#include "CloudSyncManager.h"
#endif
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "PlaylistManager.h"
#include "SidMetadata.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <functional>
#include <unordered_set>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cmath>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;

UIManager::UIManager(SidPlayer& player, PlaylistManager& playlist, BackgroundManager& background, FileBrowser& fileBrowser, DatabaseManager& database, HistoryManager& history, RatingManager& ratingManager)
    : m_player(player), m_playlist(playlist), m_background(background), m_fileBrowser(fileBrowser), m_database(database), m_history(history), m_ratingManager(ratingManager),
      m_window(nullptr), m_renderer(nullptr), m_showFileDialog(false), m_isConfigTabActive(false), m_indexRequested(false), m_showDebugWindow(false),
      m_selectedSearchResult(-1), m_searchListFocused(false), m_searchPending(false),
      m_databaseOperationInProgress(false), m_databaseOperationProgress(0.0f),
      m_filtersNeedUpdate(true), 
      m_authorFilterWidget("Author", 200.0f), 
      m_yearFilterWidget("Year", 150.0f),
      m_filterRating(0), m_filterRatingOperator(true),  // 0 = pas de filtre, true = >= par défaut
      m_filtersActive(false),
      m_shouldFocusPlaylist(false),
      m_flatListValid(false),        // Virtual Scrolling : liste plate invalide au départ
      m_visibleIndicesValid(false),  // Virtual Scrolling : liste d'indices invalide au départ
      m_cachedCurrentIndex(-1), m_navigationCacheValid(false),
      m_currentFPS(0.0f), m_oscilloscopeTime(0.0f), m_oscilloscopePlot0Time(0.0f), 
      m_oscilloscopePlot1Time(0.0f), m_oscilloscopePlot2Time(0.0f),
      m_rainbowCycleOffset(0) {
    generateRainbowPalette();
}

bool UIManager::initialize(SDL_Window* window, SDL_Renderer* renderer) {
    m_window = window;
    m_renderer = renderer;
    
    // Initialiser ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Charger une police plus grande
    float fontSize = 24.0f;
    io.Fonts->AddFontDefault();
    
    // Initialiser la police bold à nullptr (pour l'instant, on utilisera juste la couleur pour distinguer les dossiers)
    // TODO: Charger une police bold depuis un fichier si nécessaire
    m_boldFont = nullptr;
    
    // Charger FontAwesome avec MergeMode pour fusionner avec la police par défaut
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphOffset = ImVec2(0, 6.0f);
    
    // Chercher la police FontAwesome dans plusieurs emplacements possibles
    std::vector<fs::path> fontPaths = {
        fs::current_path() / "fonts" / "fa-solid-900.ttf",
        fs::current_path().parent_path() / "fonts" / "fa-solid-900.ttf",
        fs::current_path().parent_path().parent_path() / "fonts" / "fa-solid-900.ttf",
        fs::current_path() / "fa-solid-900.ttf",
        fs::current_path().parent_path() / "fa-solid-900.ttf",
        fs::current_path().parent_path().parent_path() / "fa-solid-900.ttf"
    };
    
    fs::path fontPath;
    bool fontFound = false;
    for (const auto& path : fontPaths) {
        if (fs::exists(path)) {
            fontPath = path;
            fontFound = true;
            break;
        }
    }
    
    if (fontFound) {
        io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), fontSize, &icons_config, icons_ranges);
        UI_LOG_INFO("FontAwesome chargé depuis: {}", fontPath.string());
    } else {
        UI_LOG_WARNING("FontAwesome non trouvé, les icônes ne seront pas disponibles");
    }
    
    io.FontGlobalScale = 1.2f;

    // Style ImGui
    ImGui::StyleColorsDark();
    
    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(15.0f, 10.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.WindowPadding = ImVec2(20.0f, 20.0f);
    style.FrameRounding = 5.0f;
    
    // Rendre ImGui transparent pour voir l'image de fond
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg].w = 0.0f;
    colors[ImGuiCol_ChildBg].w = 0.0f;
    colors[ImGuiCol_PopupBg].w = 0.8f;
    colors[ImGuiCol_FrameBg].w = 0.3f;
    colors[ImGuiCol_FrameBgHovered].w = 0.4f;
    colors[ImGuiCol_FrameBgActive].w = 0.5f;
    colors[ImGuiCol_Button].w = 0.4f;
    colors[ImGuiCol_ButtonHovered].w = 0.6f;
    colors[ImGuiCol_ButtonActive].w = 0.7f;
    colors[ImGuiCol_Tab].w = 0.3f;
    colors[ImGuiCol_TabHovered].w = 0.5f;
    colors[ImGuiCol_TabActive].w = 0.6f;
    colors[ImGuiCol_Header].w = 0.4f;
    colors[ImGuiCol_HeaderHovered].w = 0.5f;
    colors[ImGuiCol_HeaderActive].w = 0.6f;

    // Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    
    return true;
}

void UIManager::render() {
    auto frameStart = std::chrono::high_resolution_clock::now();
    
    // Réinitialiser isConfigTabActive au début de chaque frame
    m_isConfigTabActive = false;
    
    // Gérer le cyclage arc-en-ciel des étoiles (si activé)
    Config& config = Config::getInstance();
    bool rainbowEnabled = config.isStarRatingRainbow();
    static int rainbowFrameCount = 0;
    if (rainbowEnabled) {
        int cycleFreq = config.getStarRatingRainbowCycleFreq();
        if (cycleFreq > 0) {
            rainbowFrameCount++;
            if (rainbowFrameCount >= cycleFreq) {
                m_rainbowCycleOffset = (m_rainbowCycleOffset + 1) % 255;
                rainbowFrameCount = 0;
            }
        }
    }
    
    // Démarrer le frame ImGui
    auto t0 = std::chrono::high_resolution_clock::now();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    auto t1 = std::chrono::high_resolution_clock::now();
    auto newFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    
    // Render main panel
    auto t2 = std::chrono::high_resolution_clock::now();
    renderMainPanel();
    auto t3 = std::chrono::high_resolution_clock::now();
    auto mainPanelTime = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    
    // Render playlist panel
    auto t4 = std::chrono::high_resolution_clock::now();
    renderPlaylistPanel();
    auto t5 = std::chrono::high_resolution_clock::now();
    auto playlistPanelTime = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
    
    // Render file browser
    auto t6 = std::chrono::high_resolution_clock::now();
    renderFileBrowser();
    auto t7 = std::chrono::high_resolution_clock::now();
    auto fileBrowserTime = std::chrono::duration_cast<std::chrono::microseconds>(t7 - t6).count();
    
    // Stocker les timings pour la fenêtre de debug (utilisés à la frame suivante)
    static long long storedNewFrameTime = 0, storedMainPanelTime = 0, storedPlaylistPanelTime = 0;
    static long long storedFileBrowserTime = 0, storedImguiRenderTime = 0, storedClearTime = 0;
    static long long storedBackgroundTime = 0, storedRenderDrawDataTime = 0, storedPresentTime = 0;
    static long long storedTotalFrameTime = 0;
    
    // Fenêtre de debug (toggle avec Alt) - utilise les timings de la frame précédente
    ImGuiIO& io = ImGui::GetIO();
    // Détecter un appui unique sur Alt (pas maintenu) pour toggle
    // Utiliser IsKeyPressed pour détecter un appui unique (retourne true seulement à la première frame où la touche est pressée)
    static bool altWasPressed = false;
    bool altPressed = io.KeyAlt;
    // Détecter si Alt vient d'être pressé (transition de false à true)
    if (altPressed && !altWasPressed) {
        // Alt vient d'être pressé, toggle la fenêtre
        m_showDebugWindow = !m_showDebugWindow;
    }
    altWasPressed = altPressed;
    
    if (m_showDebugWindow) {
        renderDebugWindow(storedNewFrameTime, storedMainPanelTime, storedPlaylistPanelTime, storedFileBrowserTime,
                         storedImguiRenderTime, storedClearTime, storedBackgroundTime, 
                         storedRenderDrawDataTime, storedPresentTime, storedTotalFrameTime);
    }
    
    // Rendu ImGui
    auto t8 = std::chrono::high_resolution_clock::now();
    ImGui::Render();
    auto t9 = std::chrono::high_resolution_clock::now();
    auto imguiRenderTime = std::chrono::duration_cast<std::chrono::microseconds>(t9 - t8).count();
    SDL_RenderSetScale(m_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    
    // Effacer le renderer
    auto t10 = std::chrono::high_resolution_clock::now();
    ImVec4 clearColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    SDL_SetRenderDrawColor(m_renderer, 
        (Uint8)(clearColor.x * 255), 
        (Uint8)(clearColor.y * 255), 
        (Uint8)(clearColor.z * 255), 
        (Uint8)(clearColor.w * 255));
    SDL_RenderClear(m_renderer);
    auto t11 = std::chrono::high_resolution_clock::now();
    auto clearTime = std::chrono::duration_cast<std::chrono::microseconds>(t11 - t10).count();
    
    // Afficher l'image de fond
    auto t12 = std::chrono::high_resolution_clock::now();
    renderBackground();
    auto t13 = std::chrono::high_resolution_clock::now();
    auto backgroundTime = std::chrono::duration_cast<std::chrono::microseconds>(t13 - t12).count();
    
    // Render ImGui draw data
    auto t14 = std::chrono::high_resolution_clock::now();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    auto t15 = std::chrono::high_resolution_clock::now();
    auto renderDrawDataTime = std::chrono::duration_cast<std::chrono::microseconds>(t15 - t14).count();
    
    // Present
    auto t16 = std::chrono::high_resolution_clock::now();
    SDL_RenderPresent(m_renderer);
    auto t17 = std::chrono::high_resolution_clock::now();
    auto presentTime = std::chrono::duration_cast<std::chrono::microseconds>(t17 - t16).count();
    
    // Vérifier les erreurs SDL après le present
    // Si une erreur est détectée, elle sera gérée par Application::run() qui vérifie périodiquement
    const char* sdlError = SDL_GetError();
    if (sdlError && strlen(sdlError) > 0) {
        // Ne logger qu'une fois toutes les 1000 frames pour éviter le spam
        static int errorLogCounter = 0;
        if (errorLogCounter++ % 1000 == 0) {
            UI_LOG_WARNING("Erreur SDL détectée après RenderPresent: {}", sdlError);
        }
        // Effacer l'erreur pour éviter qu'elle ne soit reportée en continu
        SDL_ClearError();
    }
    
    auto frameEnd = std::chrono::high_resolution_clock::now();
    auto totalFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
    
    // Mettre à jour les timings stockés pour la fenêtre de debug (frame suivante)
    // Les variables statiques sont déclarées plus haut dans la fonction
    storedNewFrameTime = newFrameTime;
    storedMainPanelTime = mainPanelTime;
    storedPlaylistPanelTime = playlistPanelTime;
    storedFileBrowserTime = fileBrowserTime;
    storedImguiRenderTime = imguiRenderTime;
    storedClearTime = clearTime;
    storedBackgroundTime = backgroundTime;
    storedRenderDrawDataTime = renderDrawDataTime;
    storedPresentTime = presentTime;
    storedTotalFrameTime = totalFrameTime;
    
    // Afficher les timings toutes les 10 frames (pour capture de référence)
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 10 == 0) {
        UI_LOG_DEBUG("=== RENDER TIMINGS (frame {}) ===", frameCount);
        UI_LOG_DEBUG("NewFrame:        {:6.2f} us ({:.2f}%)", newFrameTime / 1000.0, (newFrameTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("MainPanel:       {:6.2f} us ({:.2f}%)", mainPanelTime / 1000.0, (mainPanelTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("PlaylistPanel:   {:6.2f} us ({:.2f}%)", playlistPanelTime / 1000.0, (playlistPanelTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("FileBrowser:     {:6.2f} us ({:.2f}%)", fileBrowserTime / 1000.0, (fileBrowserTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("ImGui::Render:   {:6.2f} us ({:.2f}%)", imguiRenderTime / 1000.0, (imguiRenderTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("Clear:           {:6.2f} us ({:.2f}%)", clearTime / 1000.0, (clearTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("Background:      {:6.2f} us ({:.2f}%)", backgroundTime / 1000.0, (backgroundTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("RenderDrawData:  {:6.2f} us ({:.2f}%)", renderDrawDataTime / 1000.0, (renderDrawDataTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("Present:         {:6.2f} us ({:.2f}%)", presentTime / 1000.0, (presentTime * 100.0) / totalFrameTime);
        UI_LOG_DEBUG("TOTAL FRAME:     {:6.2f} us ({:.2f} ms, {:.1f} FPS)", totalFrameTime / 1000.0, totalFrameTime / 1000.0, 1000000.0 / totalFrameTime);
        UI_LOG_DEBUG("===================================");
    }
}

bool UIManager::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    return event.type == SDL_QUIT;
}

void UIManager::shutdown() {
    shutdownRenderer();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::shutdownRenderer() {
    ImGui_ImplSDLRenderer2_Shutdown();
}

bool UIManager::reinitializeRenderer(SDL_Renderer* renderer) {
    m_renderer = renderer;
    
    // Réinitialiser le backend ImGui SDLRenderer2
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        UI_LOG_ERROR("Échec de la réinitialisation du backend ImGui SDLRenderer2");
        return false;
    }
    
    UI_LOG_INFO("Backend ImGui SDLRenderer2 réinitialisé avec succès");
    return true;
}

void UIManager::renderMainPanel() {
    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    
    float mainPanelWidth = windowWidth * 0.7f;
    
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(mainPanelWidth, (float)windowHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("imSid Player", nullptr, 
        ImGuiWindowFlags_NoTitleBar | 
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Player")) {
            renderPlayerTab();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Config")) {
            m_isConfigTabActive = true;
            renderConfigTab();
            ImGui::EndTabItem();
        } else {
            m_isConfigTabActive = false;
        }
        
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void UIManager::renderPlayerTab() {
    auto t0 = std::chrono::high_resolution_clock::now();
    renderOscilloscopes();
    auto t1 = std::chrono::high_resolution_clock::now();
    static long long oscilloscopesTime = 0;
    oscilloscopesTime += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    
    // Section informations
    ImGui::Text("Current file:");
    if (!m_player.getCurrentFile().empty()) {
        ImGui::SameLine();
        std::string filename = fs::path(m_player.getCurrentFile()).filename().string();
        std::string clickableText = std::string(ICON_FA_MICROCHIP) + " " + std::string(ICON_FA_MUSIC) + " " + filename;
        
        // Rendre le nom cliquable
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        if (ImGui::Selectable(clickableText.c_str(), false, ImGuiSelectableFlags_None)) {
            // Au clic, positionner l'arbre sur le morceau en cours
            m_playlist.setScrollToCurrent(true);
            m_shouldFocusPlaylist = true;  // Donner le focus à la playlist à la prochaine frame
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::TextWrapped("Information: %s", m_player.getTuneInfo().c_str());
        ImGui::Text("SID Model: %s", m_player.getSidModel().c_str());
        
        // Afficher les informations de subsong si plusieurs subsongs disponibles
        if (m_player.hasMultipleSongs()) {
            int currentSong = m_player.getCurrentSong() + 1;  // Convertir en 1-based pour affichage
            int totalSongs = m_player.getTotalSongs();
            ImGui::Text("Subsong: %d / %d", currentSong, totalSongs);
        }
        
        // Récupérer la durée depuis les métadonnées (si sauvegardée) ou SongLengthDB
        const SidMetadata* metadata = m_database.getMetadata(m_player.getCurrentFile());
        if (metadata) {
            double totalDuration = -1.0;
            int subsongIndex = m_player.getCurrentSong(); // 0-based
            
            // D'abord essayer depuis les métadonnées sauvegardées
            if (!metadata->songLengths.empty() && subsongIndex < metadata->songLengths.size()) {
                totalDuration = metadata->songLengths[subsongIndex];
            } else if (!metadata->md5Hash.empty()) {
                // Fallback : chercher dans SongLengthDB
                SongLengthDB& db = SongLengthDB::getInstance();
                if (db.isLoaded()) {
                    totalDuration = db.getDuration(metadata->md5Hash, subsongIndex);
                }
            }
            
            if (totalDuration > 0.0) {
                // Calculer la progression
                float currentTime = m_player.getPlaybackTime(); // Temps écoulé en secondes
                    
                // Pour le loop : calculer le temps modulo la durée totale
                // libsidplayfp boucle naturellement, on ajuste juste l'affichage
                float displayTime = currentTime;
                if (m_player.isLoopEnabled() && totalDuration > 0.0f) {
                    displayTime = std::fmod(currentTime, totalDuration);
                }
                
                float progress = 0.0f;
                if (totalDuration > 0.0 && displayTime >= 0.0f) {
                    progress = static_cast<float>(displayTime / totalDuration);
                    progress = std::max(0.0f, std::min(1.0f, progress)); // Clamp entre 0 et 1
                }
                
                // Formater le temps actuel et total
                int currentMinutes = static_cast<int>(displayTime) / 60;
                int currentSeconds = static_cast<int>(displayTime) % 60;
                int totalMinutes = static_cast<int>(totalDuration) / 60;
                int totalSeconds = static_cast<int>(totalDuration) % 60;
                
                // Formater le texte centré
                char progressText[64];
                snprintf(progressText, sizeof(progressText), "%d:%02d / %d:%02d", 
                        currentMinutes, currentSeconds, totalMinutes, totalSeconds);
                
                // Créer une barre de progression personnalisée avec dégradé pastel
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float barWidth = ImGui::GetContentRegionAvail().x;
                float barHeight = 16.0f; // Hauteur réduite
                ImVec2 barMin = pos;
                ImVec2 barMax = ImVec2(pos.x + barWidth, pos.y + barHeight);
                
                // Dessiner le fond de la barre
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImU32 bgColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
                drawList->AddRectFilled(barMin, barMax, bgColor, ImGui::GetStyle().FrameRounding);
                
                // Dessiner la barre de progression avec dégradé pastel arc-en-ciel
                if (progress > 0.0f) {
                    float fillWidth = barWidth * progress;
                    ImVec2 fillMax = ImVec2(barMin.x + fillWidth, barMax.y);
                    
                    // Obtenir l'option d'animation depuis Config
                    Config& config = Config::getInstance();
                    bool animated = config.isProgressBarAnimated();
                    
                    if (animated) {
                        // Dégradé arc-en-ciel cyclique avec couleurs pastel tamisées
                        // Cycle basé sur le temps pour créer un effet animé
                        static auto cycleStart = std::chrono::steady_clock::now();
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cycleStart).count();
                        float cyclePhase = (elapsed % 3000) / 3000.0f; // Cycle de 3 secondes
                        
                        // Couleurs arc-en-ciel pastel tamisées (HSV → RGB)
                        // Format : doux et discret, saturation et luminosité réduites
                        auto getRainbowColor = [](float hue, float sat = 0.30f, float val = 0.55f) -> ImU32 {
                            // HSV to RGB (hue en 0-1, saturation et value réduites pour pastel tamisé)
                            float c = val * sat;
                            float x = c * (1.0f - std::abs(std::fmod(hue * 6.0f, 2.0f) - 1.0f));
                            float m = val - c;
                            
                            float r = 0.0f, g = 0.0f, b = 0.0f;
                            if (hue < 1.0f/6.0f) { r = c; g = x; b = 0; }
                            else if (hue < 2.0f/6.0f) { r = x; g = c; b = 0; }
                            else if (hue < 3.0f/6.0f) { r = 0; g = c; b = x; }
                            else if (hue < 4.0f/6.0f) { r = 0; g = x; b = c; }
                            else if (hue < 5.0f/6.0f) { r = x; g = 0; b = c; }
                            else { r = c; g = 0; b = x; }
                            
                            return IM_COL32(
                                static_cast<int>((r + m) * 255),
                                static_cast<int>((g + m) * 255),
                                static_cast<int>((b + m) * 255),
                                180 // Opacité modérée pour rester discret
                            );
                        };
                        
                        // Créer un dégradé arc-en-ciel avec plusieurs segments pour plus de fluidité
                        int segments = 50; // Plus de segments pour un dégradé plus fluide
                        for (int i = 0; i < segments; ++i) {
                            float t0 = static_cast<float>(i) / segments;
                            float t1 = static_cast<float>(i + 1) / segments;
                            
                            ImVec2 p0 = ImVec2(barMin.x + fillWidth * t0, barMin.y);
                            ImVec2 p1 = ImVec2(barMin.x + fillWidth * t1, barMax.y);
                            
                            // Dégradé arc-en-ciel qui cycle dans la barre
                            // Le cycle se déplace de gauche à droite
                            float hue0 = std::fmod(cyclePhase + t0 * 0.7f, 1.0f); // Arc-en-ciel sur 70% du spectre
                            float hue1 = std::fmod(cyclePhase + t1 * 0.7f, 1.0f);
                            
                            ImU32 color0 = getRainbowColor(hue0);
                            ImU32 color1 = getRainbowColor(hue1);
                            
                            // Dégradé entre les deux couleurs pour chaque segment
                            drawList->AddRectFilledMultiColor(p0, p1, color0, color1, color1, color0);
                        }
                    } else {
                        // Dégradé statique discret (bleu pastel vers vert-cyan pastel)
                        ImU32 colorLeft = IM_COL32(140, 160, 180, 180);   // Bleu pastel tamisé (gauche)
                        ImU32 colorRight = IM_COL32(120, 180, 170, 180);  // Vert-cyan pastel tamisé (droite)
                        
                        drawList->AddRectFilledMultiColor(
                            barMin,
                            fillMax,
                            colorLeft,
                            colorRight,
                            colorRight,
                            colorLeft
                        );
                    }
                }
                
                // Dessiner le contour
                ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
                drawList->AddRect(barMin, barMax, borderColor, ImGui::GetStyle().FrameRounding, 0, 1.0f);
                
                // Centrer le texte dans la barre
                ImVec2 textSize = ImGui::CalcTextSize(progressText);
                ImVec2 textPos = ImVec2(
                    barMin.x + (barWidth - textSize.x) * 0.5f,
                    barMin.y + (barHeight - textSize.y) * 0.5f
                );
                
                // Dessiner l'ombre du texte pour meilleure lisibilité
                drawList->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 128), progressText);
                drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), progressText);
                
                // Déplacer le curseur après la barre
                ImGui::SetCursorScreenPos(ImVec2(barMin.x, barMax.y + ImGui::GetStyle().ItemSpacing.y));
                
                // Détecter la fin du morceau et gérer le passage au suivant (si loop désactivé)
                // Si loop est actif, libsidplayfp boucle naturellement, on ne fait rien
                static bool songEnded = false; // Variable statique pour éviter les déclenchements multiples
                if (!m_player.isLoopEnabled() && m_player.isPlaying() && !m_player.isPaused() && progress >= 0.99f && currentTime >= totalDuration - 0.5f) {
                    // Le morceau est terminé (avec une petite marge de 0.5s) - seulement si loop désactivé
                    if (!songEnded) { // Éviter les déclenchements multiples
                        songEnded = true;
                        
                        // Loop désactivé : passer au morceau suivant
                        PlaylistNode* nextNode = getNextFilteredFile();
                        if (nextNode && !nextNode->filepath.empty()) {
                            // Toujours trouver le nœud correspondant dans l'arbre original pour setCurrentNode
                            // (même sans filtres, pour s'assurer qu'on utilise le bon pointeur)
                            PlaylistNode* originalNode = m_playlist.findNodeByPath(nextNode->filepath);
                            PlaylistNode* targetNode = originalNode ? originalNode : nextNode;
                            m_playlist.setCurrentNode(targetNode);
                            m_playlist.setScrollToCurrent(true);
                            if (m_player.loadFile(nextNode->filepath)) {
                                m_player.play();
                                recordHistoryEntry(nextNode->filepath);
                            }
                        } else {
                            // Pas de morceau suivant, arrêter
                            m_player.stop();
                        }
                    }
                } else {
                    // Réinitialiser le flag si on n'est plus à la fin
                    songEnded = false;
                }
            } else {
                // Durée non trouvée
                float currentTime = m_player.getPlaybackTime();
                if (currentTime > 0.0f) {
                    int currentMinutes = static_cast<int>(currentTime) / 60;
                    int currentSeconds = static_cast<int>(currentTime) % 60;
                    ImGui::Text("Duration: Unknown (Playing: %d:%02d)", currentMinutes, currentSeconds);
                } else {
                    ImGui::Text("Duration: Unknown");
                }
            }
        } else {
            // Pas de métadonnées ou pas de MD5
            float currentTime = m_player.getPlaybackTime();
            if (currentTime > 0.0f) {
                int currentMinutes = static_cast<int>(currentTime) / 60;
                int currentSeconds = static_cast<int>(currentTime) % 60;
                ImGui::Text("Duration: Unknown (Playing: %d:%02d)", currentMinutes, currentSeconds);
            } else {
                ImGui::Text("Duration: Unknown");
            }
        }
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No file loaded");
    }
    
    ImGui::Separator();
    ImGui::Spacing();

    auto t2 = std::chrono::high_resolution_clock::now();
    renderPlayerControls();
    auto t3 = std::chrono::high_resolution_clock::now();
    static long long playerControlsTime = 0;
    playerControlsTime += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    
    // Afficher les timings accumulés toutes les 60 frames
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        UI_LOG_DEBUG("[PlayerTab] Oscilloscopes: {:.2f} us/frame avg, PlayerControls: {:.2f} us/frame avg",
                  oscilloscopesTime / 60.0 / 1000.0, playerControlsTime / 60.0 / 1000.0);
        oscilloscopesTime = 0;
        playerControlsTime = 0;
    }
}

void UIManager::renderOscilloscopes() {
    if (m_player.getCurrentFile().empty()) return;
    
    auto oscStart = std::chrono::high_resolution_clock::now();
    
    ImGui::Text("Oscilloscopes by voice:");
    ImGui::Spacing();
    
    const float* voice0 = m_player.getVoiceBuffer(0);
    const float* voice1 = m_player.getVoiceBuffer(1);
    const float* voice2 = m_player.getVoiceBuffer(2);
    
    float plotHeight = 120.0f;
    float plotWidth = ImGui::GetContentRegionAvail().x / 3.0f - 5.0f;
    
    bool voice0Active = m_player.isVoiceMuted(0);
    bool voice1Active = m_player.isVoiceMuted(1);
    bool voice2Active = m_player.isVoiceMuted(2);
    
    bool prev0 = voice0Active;
    bool prev1 = voice1Active;
    bool prev2 = voice2Active;
    
    // Voix 0
    ImGui::PushID("voice0");
    ImVec2 plotPos0 = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(plotPos0.x + plotWidth - 20.0f, plotPos0.y + 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    ImGui::Checkbox("##mute", &voice0Active);
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(plotPos0);
    ImVec4 bgColor = ImVec4(0.1f, 0.1f, 0.1f, voice0Active ? 0.1f : 0.3f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.3f, 0.3f, 0.3f, voice0Active ? 0.3f : 1.0f));
    if (ImGui::InvisibleButton("##clickable0", ImVec2(plotWidth, plotHeight))) {
        voice0Active = !voice0Active;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mMin = ImGui::GetItemRectMin();
        ImVec2 mMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(mMin, mMax, IM_COL32(0, 0, 0, 60), 5.0f);
        drawList->AddRect(mMin, mMax, IM_COL32(255, 255, 255, 30), 5.0f);
    }
    ImGui::SetCursorScreenPos(plotPos0);
    auto t0 = std::chrono::high_resolution_clock::now();
    ImGui::PlotLines("##plot", voice0, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                    ImVec2(plotWidth, plotHeight));
    auto t1 = std::chrono::high_resolution_clock::now();
    static long long plot0Time = 0;
    plot0Time += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    ImGui::PopStyleColor(2);
    ImGui::GetWindowDrawList()->AddText(ImVec2(plotPos0.x + 5.0f, plotPos0.y + 5.0f), 
                                       IM_COL32(255, 255, 255, 255), "0");
    ImGui::PopID();
    
    ImGui::SameLine();
    
    // Voix 1
    ImGui::PushID("voice1");
    ImVec2 plotPos1 = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(plotPos1.x + plotWidth - 20.0f, plotPos1.y + 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    ImGui::Checkbox("##mute", &voice1Active);
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(plotPos1);
    ImVec4 bgColor1 = ImVec4(0.1f, 0.1f, 0.1f, voice1Active ? 0.1f : 0.3f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 1.0f, 0.3f, voice1Active ? 0.3f : 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    if (ImGui::InvisibleButton("##clickable1", ImVec2(plotWidth, plotHeight))) {
        voice1Active = !voice1Active;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mMin = ImGui::GetItemRectMin();
        ImVec2 mMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(mMin, mMax, IM_COL32(0, 0, 0, 60), 5.0f);
        drawList->AddRect(mMin, mMax, IM_COL32(255, 255, 255, 30), 5.0f);
    }
    ImGui::SetCursorScreenPos(plotPos1);
    auto t2 = std::chrono::high_resolution_clock::now();
    ImGui::PlotLines("##plot", voice1, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                    ImVec2(plotWidth, plotHeight));
    auto t3 = std::chrono::high_resolution_clock::now();
    static long long plot1Time = 0;
    plot1Time += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::GetWindowDrawList()->AddText(ImVec2(plotPos1.x + 5.0f, plotPos1.y + 5.0f), 
                                       IM_COL32(255, 255, 255, 255), "1");
    ImGui::PopID();
    
    ImGui::SameLine();
    
    // Voix 2
    ImGui::PushID("voice2");
    ImVec2 plotPos2 = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(plotPos2.x + plotWidth - 20.0f, plotPos2.y + 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    ImGui::Checkbox("##mute", &voice2Active);
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(plotPos2);
    ImVec4 bgColor2 = ImVec4(0.1f, 0.1f, 0.1f, voice1Active ? 0.1f : 0.3f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bgColor2);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.3f, 1.0f, voice2Active ? 0.3f : 1.0f));
    if (ImGui::InvisibleButton("##clickable2", ImVec2(plotWidth, plotHeight))) {
        voice2Active = !voice2Active;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mMin = ImGui::GetItemRectMin();
        ImVec2 mMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(mMin, mMax, IM_COL32(0, 0, 0, 60), 5.0f);
        drawList->AddRect(mMin, mMax, IM_COL32(255, 255, 255, 30), 5.0f);
    }
    ImGui::SetCursorScreenPos(plotPos2);
    auto t4 = std::chrono::high_resolution_clock::now();
    ImGui::PlotLines("##plot", voice2, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                    ImVec2(plotWidth, plotHeight));
    auto t5 = std::chrono::high_resolution_clock::now();
    static long long plot2Time = 0;
    plot2Time += std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
    ImGui::PopStyleColor(2);
    ImGui::GetWindowDrawList()->AddText(ImVec2(plotPos2.x + 5.0f, plotPos2.y + 5.0f), 
                                       IM_COL32(255, 255, 255, 255), "2");
    ImGui::PopID();
    
    if (voice0Active != prev0) m_player.setVoiceMute(0, voice0Active);
    if (voice1Active != prev1) m_player.setVoiceMute(1, voice1Active);
    if (voice2Active != prev2) m_player.setVoiceMute(2, voice2Active);
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    auto oscEnd = std::chrono::high_resolution_clock::now();
    static long long totalOscTime = 0;
    totalOscTime += std::chrono::duration_cast<std::chrono::microseconds>(oscEnd - oscStart).count();
    
    // Afficher les timings accumulés toutes les 60 frames et mettre à jour les variables membres
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        // Convertir en millisecondes et stocker dans les variables membres
        m_oscilloscopeTime = totalOscTime / 60.0 / 1000.0;
        m_oscilloscopePlot0Time = plot0Time / 60.0 / 1000.0;
        m_oscilloscopePlot1Time = plot1Time / 60.0 / 1000.0;
        m_oscilloscopePlot2Time = plot2Time / 60.0 / 1000.0;
        
        UI_LOG_DEBUG("[Oscilloscopes] Total: {:.2f} us/frame avg, Plot0: {:.2f} us, Plot1: {:.2f} us, Plot2: {:.2f} us",
                  totalOscTime / 60.0 / 1000.0, plot0Time / 60.0 / 1000.0, 
                  plot1Time / 60.0 / 1000.0, plot2Time / 60.0 / 1000.0);
        totalOscTime = 0;
        plot0Time = 0;
        plot1Time = 0;
        plot2Time = 0;
    }
}

void UIManager::renderPlayerControls() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 12.0f));
    
    if (m_player.getCurrentFile().empty()) {
        ImGui::BeginDisabled();
    }

    float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    
    if (m_player.isPlaying() && !m_player.isPaused()) {
        if (ImGui::Button(ICON_FA_PAUSE "", ImVec2(buttonWidth, 0))) {
            m_player.pause();
        }
    } else {
        if (ImGui::Button(ICON_FA_PLAY "", ImVec2(buttonWidth, 0))) {
            m_player.play();
        }
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button(ICON_FA_STOP "", ImVec2(buttonWidth, 0))) {
        m_player.stop();
    }
    
    ImGui::SameLine();
    
    // Bouton Loop / Non-Loop
    bool loopEnabled = m_player.isLoopEnabled();
    ImVec4 loopButtonColor = loopEnabled ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, loopButtonColor);
    if (ImGui::Button(ICON_FA_REPEAT "", ImVec2(buttonWidth, 0))) {
        bool newLoopState = !loopEnabled;
        m_player.setLoop(newLoopState);
        // Sauvegarder l'état dans la config
        Config& config = Config::getInstance();
        config.setLoopEnabled(newLoopState);
        fs::path configDir = getConfigDir();
        std::string configPath = (configDir / "config.txt").string();
        config.save(configPath);
    }
    ImGui::PopStyleColor();

    if (m_player.getCurrentFile().empty()) {
        ImGui::EndDisabled();
    }
    
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::Separator();

    // Contrôles de navigation des subsongs
    if (!m_player.getCurrentFile().empty() && m_player.hasMultipleSongs()) {
        ImGui::Text("Subsong Navigation:");
        
        int currentSong = m_player.getCurrentSong() + 1;  // 1-based
        int totalSongs = m_player.getTotalSongs();
        
        // Largeur fixe pour les boutons (plus prévisible)
        float buttonWidth = 100.0f;
        float availableWidth = ImGui::GetContentRegionAvail().x;
        float comboWidth = availableWidth - (buttonWidth * 2) - (ImGui::GetStyle().ItemSpacing.x * 2);
        comboWidth = std::max(120.0f, std::min(comboWidth, 200.0f));  // Entre 120 et 200 pixels
        
        // Bouton Précédent
        if (ImGui::Button(ICON_FA_BACKWARD_STEP " Prev", ImVec2(buttonWidth, 0))) {
            m_player.prevSong();
        }
        
        ImGui::SameLine();
        
        // Sélecteur de subsong (Combo) avec largeur limitée
        char subsongLabel[32];
        snprintf(subsongLabel, sizeof(subsongLabel), "Subsong %d/%d", currentSong, totalSongs);
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::BeginCombo("##subsong", subsongLabel)) {
            for (int i = 1; i <= totalSongs; i++) {
                char itemLabel[32];
                snprintf(itemLabel, sizeof(itemLabel), "Subsong %d", i);
                bool isSelected = (i == currentSong);
                if (ImGui::Selectable(itemLabel, isSelected)) {
                    m_player.selectSong(i);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::SameLine();
        
        // Bouton Suivant
        if (ImGui::Button(ICON_FA_FORWARD_STEP " Next", ImVec2(buttonWidth, 0))) {
            m_player.nextSong();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
    }

    ImGui::Text("State:");
    ImGui::SameLine();
    if (m_player.isPlaying()) {
        if (m_player.isPaused()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Paused");
        } else {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Playing");
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stopped");
    }
    
    if (!m_player.getCurrentFile().empty()) {
        ImGui::Text("Audio Engine:");
        ImGui::Spacing();
        
        int engineMode = m_player.isUsingMasterEngine() ? 1 : 0;
        int prevEngineMode = engineMode;
        
        ImGui::RadioButton("Master Engine (Native)", &engineMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Mixed (3 voices)", &engineMode, 0);
        
        if (engineMode != prevEngineMode) {
            m_player.setUseMasterEngine(engineMode == 1);
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Widget de notation par étoiles
        const SidMetadata* metadata = m_database.getMetadata(m_player.getCurrentFile());
        if (metadata) {
            int currentRating = m_ratingManager.getRating(metadata->metadataHash);
            int prevRating = currentRating;
            
            ImGui::Text("Rating:");
            if (renderStarRating("##trackRating", &currentRating, 5)) {
                // Le rating a changé, sauvegarder
                if (currentRating != prevRating) {
                    m_ratingManager.updateRating(metadata->metadataHash, currentRating);
                    UI_LOG_INFO("Rating mis à jour: {} étoiles pour {}", currentRating, metadata->title);
                    
#ifdef ENABLE_CLOUD_SAVE
                    // Push automatique vers le cloud
                    CloudSyncManager::getInstance().queueRatingSync();
#endif
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        } else if (!m_player.getCurrentFile().empty()) {
            // Debug : vérifier pourquoi les métadonnées ne sont pas disponibles
            UI_LOG_DEBUG("Métadonnées non disponibles pour le fichier: {}", m_player.getCurrentFile());
            ImGui::Text("Rating: Not available (file not indexed)");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }
}

void UIManager::renderConfigTab() {
    ImGui::Text("Configuration");
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::Text("Background image");
    ImGui::Separator();
    
    ImGui::Text("Drag & drop image files here to add them");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 0.3f));
    ImGui::BeginChild("DropZone", ImVec2(-1, 60), true);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
        "Drop PNG, JPG, JPEG, BMP, GIF files here");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    
    const auto& images = m_background.getImages();
    if (images.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.5f, 1.0f), "Aucune image trouvée dans background/");
    } else {
        int currentIndex = m_background.getCurrentIndex();
        ImGui::Text("Current image:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
            "%d/%d - %s", 
            currentIndex + 1, 
            (int)images.size(),
            images[currentIndex].filename.c_str());
        
        ImGui::Spacing();
        
        if (ImGui::Button(ICON_FA_CHEVRON_LEFT, ImVec2(50, 0))) {
            int newIndex = (currentIndex - 1 + images.size()) % images.size();
            m_background.setCurrentIndex(newIndex);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHEVRON_RIGHT, ImVec2(50, 0))) {
            int newIndex = (currentIndex + 1) % images.size();
            m_background.setCurrentIndex(newIndex);
        }
        
        ImGui::Spacing();
        
        bool showBg = m_background.isShown();
        if (ImGui::Button(showBg ? ICON_FA_EYE_SLASH " Hide" : ICON_FA_EYE " Show", ImVec2(120, 0))) {
            m_background.setShown(!showBg);
        }
        
    ImGui::Spacing();
    ImGui::Separator();
    
    int alpha = m_background.getAlpha();
    ImGui::Text("Transparency:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%d%%", 
        (int)((alpha / 255.0f) * 100.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f)); // Réduire la hauteur
    ImGui::PushItemWidth(150.0f); // Slider plus petit en largeur
    if (ImGui::SliderInt("##alpha", &alpha, 0, 255, "%d")) {
        m_background.setAlpha(alpha);
    }
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Section Player Settings
    ImGui::Text("Player Settings");
    ImGui::Separator();
    
    Config& config = Config::getInstance();
    bool progressBarAnimated = config.isProgressBarAnimated();
    if (ImGui::Checkbox("Animated progress bar (rainbow gradient)", &progressBarAnimated)) {
        config.setProgressBarAnimated(progressBarAnimated);
        // Sauvegarder la config
        fs::path configDir = getConfigDir();
        std::string configPath = (configDir / "config.txt").string();
        config.save(configPath);
    }
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Enable animated rainbow gradient in progress bar");
    
    ImGui::Spacing();
    
    bool starRatingRainbow = config.isStarRatingRainbow();
    if (ImGui::Checkbox("Rainbow star rating (cycling colors)", &starRatingRainbow)) {
        config.setStarRatingRainbow(starRatingRainbow);
        // Sauvegarder la config
        fs::path configDir = getConfigDir();
        std::string configPath = (configDir / "config.txt").string();
        config.save(configPath);
    }
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Enable rainbow cycling colors for star ratings");
    
    if (starRatingRainbow) {
        ImGui::Spacing();
        
        int rainbowStep = config.getStarRatingRainbowStep();
        ImGui::Text("Color spacing:");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f)); // Réduire la hauteur
        ImGui::PushItemWidth(150.0f); // Slider plus petit en largeur
        if (ImGui::SliderInt("##rainbow_step", &rainbowStep, 0, 51, "%d")) {
            config.setStarRatingRainbowStep(rainbowStep);
            // Sauvegarder la config
            fs::path configDir = getConfigDir();
            std::string configPath = (configDir / "config.txt").string();
            config.save(configPath);
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Index spacing between stars (0-51)");
        
        ImGui::Spacing();
        
        int cycleFreq = config.getStarRatingRainbowCycleFreq();
        ImGui::Text("Cycle frequency:");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f)); // Réduire la hauteur
        ImGui::PushItemWidth(150.0f); // Slider plus petit en largeur
        if (ImGui::SliderInt("##rainbow_cycle_freq", &cycleFreq, 0, 20, "%d")) {
            config.setStarRatingRainbowCycleFreq(cycleFreq);
            // Sauvegarder la config
            fs::path configDir = getConfigDir();
            std::string configPath = (configDir / "config.txt").string();
            config.save(configPath);
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Cycle every N frames (0-20, 0=disabled)");
        
        ImGui::Spacing();
        
        int rainbowOffset = config.getStarRatingRainbowOffset();
        ImGui::Text("Color offset:");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f)); // Réduire la hauteur
        ImGui::PushItemWidth(150.0f); // Slider plus petit en largeur
        if (ImGui::SliderInt("##rainbow_offset", &rainbowOffset, 0, 255, "%d")) {
            config.setStarRatingRainbowOffset(rainbowOffset);
            // Sauvegarder la config
            fs::path configDir = getConfigDir();
            std::string configPath = (configDir / "config.txt").string();
            config.save(configPath);
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Offset to add to color indices (0-255)");
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
#ifdef ENABLE_CLOUD_SAVE
    // Section Cloud Save
    ImGui::Text("Cloud Save");
    ImGui::Separator();
    
    bool cloudSaveEnabled = config.isCloudSaveEnabled();
    if (ImGui::Checkbox("Enable cloud save", &cloudSaveEnabled)) {
        config.setCloudSaveEnabled(cloudSaveEnabled);
        // Sauvegarder la config
        fs::path configDir = getConfigDir();
        std::string configPath = (configDir / "config.txt").string();
        config.save(configPath);
        
        // Mettre à jour CloudSyncManager
        CloudSyncManager::getInstance().setEnabled(cloudSaveEnabled);
    }
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Synchronize ratings and history to cloud");
    
    if (cloudSaveEnabled) {
        ImGui::Spacing();
        
        // Rating endpoint
        char ratingEndpoint[512];
        strncpy(ratingEndpoint, config.getCloudRatingEndpoint().c_str(), sizeof(ratingEndpoint) - 1);
        ratingEndpoint[sizeof(ratingEndpoint) - 1] = '\0';
        ImGui::Text("Rating endpoint:");
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##rating_endpoint", ratingEndpoint, sizeof(ratingEndpoint))) {
            config.setCloudRatingEndpoint(ratingEndpoint);
            // Sauvegarder la config
            fs::path configDir = getConfigDir();
            std::string configPath = (configDir / "config.txt").string();
            config.save(configPath);
            
            // Mettre à jour CloudSyncManager
            CloudSyncManager::getInstance().setRatingEndpoint(ratingEndpoint);
        }
        ImGui::PopItemWidth();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "npoint.io endpoint URL for ratings (e.g., https://api.npoint.io/...)");
        
        ImGui::Spacing();
        
        // History endpoint
        char historyEndpoint[512];
        strncpy(historyEndpoint, config.getCloudHistoryEndpoint().c_str(), sizeof(historyEndpoint) - 1);
        historyEndpoint[sizeof(historyEndpoint) - 1] = '\0';
        ImGui::Text("History endpoint:");
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##history_endpoint", historyEndpoint, sizeof(historyEndpoint))) {
            config.setCloudHistoryEndpoint(historyEndpoint);
            // Sauvegarder la config
            fs::path configDir = getConfigDir();
            std::string configPath = (configDir / "config.txt").string();
            config.save(configPath);
            
            // Mettre à jour CloudSyncManager
            CloudSyncManager::getInstance().setHistoryEndpoint(historyEndpoint);
        }
        ImGui::PopItemWidth();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "npoint.io endpoint URL for history (e.g., https://api.npoint.io/...)");
        
        ImGui::Spacing();
        
        // Status display
        auto& cloudSync = CloudSyncManager::getInstance();
        CloudSyncManager::SyncStatus ratingStatus = cloudSync.getRatingStatus();
        CloudSyncManager::SyncStatus historyStatus = cloudSync.getHistoryStatus();
        
        ImGui::Text("Status:");
        ImGui::SameLine();
        const char* ratingStatusText = "Idle";
        ImVec4 ratingStatusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        switch (ratingStatus) {
            case CloudSyncManager::SyncStatus::Idle:
                ratingStatusText = "Idle";
                ratingStatusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Syncing:
                ratingStatusText = "Syncing";
                ratingStatusColor = ImVec4(0.3f, 0.7f, 0.9f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Success:
                ratingStatusText = "Success";
                ratingStatusColor = ImVec4(0.3f, 0.8f, 0.3f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Error:
                ratingStatusText = "Error";
                ratingStatusColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Disabled:
                ratingStatusText = "Disabled";
                ratingStatusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                break;
        }
        ImGui::TextColored(ratingStatusColor, "Ratings: %s", ratingStatusText);
        
        ImGui::SameLine();
        const char* historyStatusText = "Idle";
        ImVec4 historyStatusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        switch (historyStatus) {
            case CloudSyncManager::SyncStatus::Idle:
                historyStatusText = "Idle";
                historyStatusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Syncing:
                historyStatusText = "Syncing";
                historyStatusColor = ImVec4(0.3f, 0.7f, 0.9f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Success:
                historyStatusText = "Success";
                historyStatusColor = ImVec4(0.3f, 0.8f, 0.3f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Error:
                historyStatusText = "Error";
                historyStatusColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f);
                break;
            case CloudSyncManager::SyncStatus::Disabled:
                historyStatusText = "Disabled";
                historyStatusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                break;
        }
        ImGui::TextColored(historyStatusColor, "History: %s", historyStatusText);
        
        if (ratingStatus == CloudSyncManager::SyncStatus::Error || historyStatus == CloudSyncManager::SyncStatus::Error) {
            ImGui::Spacing();
            std::string lastError = cloudSync.getLastError();
            if (!lastError.empty()) {
                ImGui::TextColored(ImVec4(0.8f, 0.3f, 0.3f, 1.0f), "Error: %s", lastError.c_str());
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Boutons Push et Pull
        ImGui::Text("Actions:");
        if (ImGui::Button(ICON_FA_UPLOAD " Push All", ImVec2(120, 0))) {
            // Vérifier que CloudSyncManager est initialisé
            if (!cloudSync.isEnabled() && cloudSync.getRatingEndpoint().empty() && cloudSync.getHistoryEndpoint().empty()) {
                // Essayer d'initialiser si pas encore fait
                // Note: CloudSyncManager devrait être initialisé dans Application.cpp
                UI_LOG_WARNING("CloudSyncManager may not be initialized. Please ensure it's initialized in Application.cpp");
            }
            
            // Récupérer les endpoints depuis la config (doivent être saisis manuellement)
            std::string ratingEndpoint = config.getCloudRatingEndpoint();
            std::string historyEndpoint = config.getCloudHistoryEndpoint();
            
            // Vérifier que les endpoints sont configurés
            if (ratingEndpoint.empty() || historyEndpoint.empty()) {
                UI_LOG_WARNING("Please configure both rating and history endpoints in the fields above before pushing.");
            } else {
                // Push ratings et history
                if (!ratingEndpoint.empty()) {
                    cloudSync.pushRatings();
                }
                if (!historyEndpoint.empty()) {
                    cloudSync.pushHistory();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " Pull All", ImVec2(120, 0))) {
            cloudSync.pullRatings();
            cloudSync.pullHistory();
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Push: upload to cloud | Pull: download from cloud");
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
#endif
    
    // Section Songlength database
    ImGui::Text("Songlength database");
    ImGui::Separator();
    
    ImGui::Text("Drag & drop .md5 files for songlength db");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 0.3f));
    ImGui::BeginChild("SonglengthDropZone", ImVec2(-1, 60), true);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
        "Drop Songlengths.md5 file here");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    
    // Afficher le chemin du fichier et le statut
    std::string songlengthsPath = config.getSonglengthsPath();
    
    SongLengthDB& db = SongLengthDB::getInstance();
    if (!songlengthsPath.empty()) {
        ImGui::Text("Current file:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", 
            fs::path(songlengthsPath).filename().string().c_str());
        ImGui::Text("Path:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", songlengthsPath.c_str());
        
        if (db.isLoaded()) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), 
                ICON_FA_CHECK " Loaded: %zu entries", db.getCount());
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.3f, 1.0f), 
                ICON_FA_TRIANGLE_EXCLAMATION " Not loaded");
        }
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.5f, 1.0f), 
            "No Songlengths.md5 file configured");
    }
    
    ImGui::Spacing();
}

void UIManager::renderPlaylistPanel() {
    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    float mainPanelWidth = windowWidth * 0.7f;
    float playlistPanelWidth = windowWidth - mainPanelWidth;
    
    ImGui::SetNextWindowPos(ImVec2(mainPanelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(playlistPanelWidth, (float)windowHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    
    // Donner le focus à la fenêtre de playlist si demandé
    if (m_shouldFocusPlaylist) {
        ImGui::SetNextWindowFocus();
        m_shouldFocusPlaylist = false;
    }
    
    ImGui::Begin("Playlist", nullptr, 
        ImGuiWindowFlags_NoTitleBar | 
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoCollapse);
    
    // Onglets Explorer / History
    if (ImGui::BeginTabBar("PlaylistTabs")) {
        // Onglet Explorer
        if (ImGui::BeginTabItem("Explorer")) {
            renderExplorerTab();
            ImGui::EndTabItem();
        }
        
        // Onglet History
        if (ImGui::BeginTabItem("History")) {
            renderHistoryTab();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

void UIManager::renderPlaylistTree() {
    auto renderStart = std::chrono::high_resolution_clock::now();
    
    ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
    
    // Utiliser UNIQUEMENT l'arbre original (filtrage dynamique au rendu)
    PlaylistNode* root = m_playlist.getRoot();
    PlaylistNode* currentNode = m_playlist.getCurrentNode();
    bool shouldScroll = m_playlist.shouldScrollToCurrent();
    
    // VIRTUAL SCROLLING : Construire la liste plate si nécessaire
    if (!m_flatListValid) {
        buildFlatList();
        // Invalider aussi la liste d'indices car la structure a changé
        m_visibleIndicesValid = false;
    }
    
    // FILTRAGE DYNAMIQUE : Construire la liste des indices visibles si nécessaire
    if (m_filtersActive && !m_visibleIndicesValid) {
        buildVisibleIndices();
    } else if (!m_filtersActive) {
        // Si pas de filtres, invalider la liste d'indices pour économiser la mémoire
        m_visibleIndicesValid = false;
        m_visibleIndices.clear();
    }
    
    // Si on doit scroller vers le nœud courant, ouvrir tous ses parents
    bool parentsOpened = false;
    if (shouldScroll && currentNode && !currentNode->filepath.empty()) {
        // Ouvrir tous les parents du nœud courant
        PlaylistNode* parent = currentNode->parent;
        while (parent && parent != root) {
            if (m_openNodes.find(parent) == m_openNodes.end() || !m_openNodes[parent]) {
                m_openNodes[parent] = true;
                parentsOpened = true;
            }
            parent = parent->parent;
        }
        // Si on a ouvert des parents, reconstruire la liste plate
        if (parentsOpened) {
            invalidateFlatList();
            buildFlatList();
            if (m_filtersActive) {
                invalidateVisibleIndices();
                buildVisibleIndices();
            }
        }
    }
    
    size_t nodesRendered = 0;
    
    // Calculer la hauteur d'un élément (pour le clipper)
    float itemHeight = ImGui::GetTextLineHeightWithSpacing();
    
    // Utiliser ImGuiListClipper pour le virtual scrolling
    // Utiliser la liste d'indices si filtres actifs, sinon la liste complète
    size_t listSize = m_filtersActive ? m_visibleIndices.size() : m_flatList.size();
    
    // Trouver l'index du nœud courant dans la liste pour le scroll (après reconstruction si nécessaire)
    int currentIndex = -1;
    if (shouldScroll && currentNode && !currentNode->filepath.empty()) {
        if (m_filtersActive) {
            // Chercher dans m_visibleIndices
            for (size_t i = 0; i < m_visibleIndices.size(); i++) {
                size_t realIndex = m_visibleIndices[i];
                if (realIndex < m_flatList.size() && m_flatList[realIndex].node == currentNode) {
                    currentIndex = static_cast<int>(i);
                    break;
                }
            }
        } else {
            // Chercher directement dans m_flatList
            for (size_t i = 0; i < m_flatList.size(); i++) {
                if (m_flatList[i].node == currentNode) {
                    currentIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        
        // Si le fichier n'est pas visible (à cause du filtre), désactiver le scroll immédiatement
        // pour éviter les recalculs constants
        if (currentIndex < 0) {
            m_playlist.setScrollToCurrent(false);
            shouldScroll = false;
        }
    }
    
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(listSize), itemHeight);
    
    // Si on doit scroller vers le nœud courant, forcer son inclusion dans la plage visible
    if (shouldScroll && currentIndex >= 0) {
        clipper.IncludeItemByIndex(currentIndex);
    }
    
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            if (i < 0 || i >= static_cast<int>(listSize)) continue;
            
            // Récupérer l'index réel dans m_flatList
            size_t realIndex = m_filtersActive ? m_visibleIndices[i] : i;
            if (realIndex >= m_flatList.size()) continue;
            
            const FlatNode& flatNode = m_flatList[realIndex];
            PlaylistNode* node = flatNode.node;
            if (!node) continue;
            
            nodesRendered++;
            ImGui::PushID(node);
            
            bool isCurrent = (currentNode == node && !node->filepath.empty());
            bool isSelected = (currentNode == node);
            
            // Indentation basée sur la profondeur
            // Appliquer l'indentation manuellement avec SetCursorPosX pour les dossiers et les feuilles
            float indentAmount = flatNode.depth * 15.0f;
            if (flatNode.depth > 0) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentAmount);
            }
            
            if (node->isFolder) {
                
                // Récupérer l'état d'ouverture précédent
                bool wasOpen = m_openNodes.find(node) != m_openNodes.end() && m_openNodes[node];
                
                // Forcer l'ouverture/fermeture si nécessaire (SetNextItemOpen fonctionne même si le TreeNode a déjà été créé)
                ImGui::SetNextItemOpen(wasOpen, ImGuiCond_Always);
                
                // Style pour les dossiers : couleur brillante et police bold
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.6f, 1.0f)); // Jaune brillant pour les dossiers
                if (m_boldFont) {
                    ImGui::PushFont(m_boldFont);
                }
                
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
                if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
                
                // Réduire l'espacement entre la flèche et le texte en réduisant FramePadding.x
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, ImGui::GetStyle().FramePadding.y)); // Réduire seulement le padding horizontal
                bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
                ImGui::PopStyleVar();
                
                if (m_boldFont) {
                    ImGui::PopFont();
                }
                ImGui::PopStyleColor();
                
                // Sauvegarder l'état d'ouverture et invalider si changement
                if (nodeOpen != wasOpen) {
                    m_openNodes[node] = nodeOpen;
                    invalidateFlatList();  // La structure change, reconstruire la liste
                }
                
                if (nodeOpen) {
                    ImGui::TreePop();
                }
            } else {
                // Mettre en évidence le fichier courant
                if (isCurrent) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f)); // Bleu pour le fichier courant
                }
                
                // Créer un label avec le nom et potentiellement les étoiles
                std::string label = node->name;
                
                // Récupérer le rating si disponible
                const SidMetadata* metadata = m_database.getMetadata(node->filepath);
                int fileRating = 0;
                if (metadata && metadata->metadataHash != 0) {
                    fileRating = m_ratingManager.getRating(metadata->metadataHash);
                }
                
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    m_playlist.setCurrentNode(node);
                    if (!node->filepath.empty() && m_player.loadFile(node->filepath)) {
                        m_player.play();
                        recordHistoryEntry(node->filepath);
                    }
                }
                
                // Afficher les étoiles à côté du nom si le fichier a un rating
                if (fileRating > 0) {
                    ImGui::SameLine();
                    
                    float startY = ImGui::GetCursorPosY();
                    float textLineHeight = ImGui::GetTextLineHeight();
                    float normalLineHeightSpacing = ImGui::GetTextLineHeightWithSpacing();
                
                    // 1. Début du groupe pour encapsuler les étoiles
                    ImGui::BeginGroup();
                    
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 0.0f));
                    ImGui::SetWindowFontScale(0.6f);
                    
                    float smallFontHeight = ImGui::GetTextLineHeight(); 
                    float offsetY = (textLineHeight - smallFontHeight) * 0.5f; 
                
                    // Vérifier si le cyclage arc-en-ciel est activé
                    Config& config = Config::getInstance();
                    bool rainbowEnabled = config.isStarRatingRainbow();
                    
                    for (int i = 1; i <= 5; i++) {
                        if (i > 1) ImGui::SameLine(0.0f, 1.0f);
                        // On utilise SetCursorPosY (relatif) plutôt que ScreenPos pour éviter les conflits de boundaries
                        ImGui::SetCursorPosY(startY + offsetY);
                        if (i <= fileRating) {
                            if (rainbowEnabled) {
                                // Calculer l'index dans la palette pour cette étoile
                                // Utiliser l'écart configurable entre les étoiles
                                int step = config.getStarRatingRainbowStep();
                                int offset = config.getStarRatingRainbowOffset();
                                int paletteIndex = ((i - 1) * step + m_rainbowCycleOffset + offset) % 255;
                                ImGui::PushStyleColor(ImGuiCol_Text, m_rainbowPalette[paletteIndex]);
                            } else {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
                            }
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
                        }
                        
                        ImGui::Text(ICON_FA_STAR);
                        ImGui::PopStyleColor();
                    }
                
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopStyleVar(); // Pop ItemSpacing des étoiles
                    ImGui::EndGroup();
                
                    // 2. SATISFAIRE L'ASSERTION SANS ESPACE :
                    // On force le curseur à la position de la ligne suivante
                    ImGui::SetCursorPosY(startY + normalLineHeightSpacing);
                    
                    // On utilise un Dummy(0,0) avec un ItemSpacing à zéro pour que ImGui valide la taille
                    // de la fenêtre sans ajouter de pixels de marge supplémentaires.
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
                    ImGui::Dummy(ImVec2(0.0f, 0.0f));
                    ImGui::PopStyleVar();
                }
                
                // Scroller vers le fichier courant
                if (isCurrent && shouldScroll) {
                    ImGui::SetScrollHereY(0.5f);
                }
                
                if (isCurrent) {
                    ImGui::PopStyleColor();
                }
            }
            
            ImGui::PopID();
        }
    }
    
    clipper.End();
    
    if (shouldScroll) {
        m_playlist.setScrollToCurrent(false);
    }
    
    auto renderEnd = std::chrono::high_resolution_clock::now();
    auto renderTime = std::chrono::duration_cast<std::chrono::milliseconds>(renderEnd - renderStart).count();
    
    // Log pour mesurer le problème (toujours en DEBUG pour capture de référence)
    UI_LOG_DEBUG("[UI] renderPlaylistTree: {} ms ({} nœuds rendus, {} dans liste plate, {} visibles, filtrage: {}, virtual scrolling: activé)", 
              renderTime, nodesRendered, m_flatList.size(), m_filtersActive ? m_visibleIndices.size() : m_flatList.size(), m_filtersActive ? "dynamique" : "désactivé");
    if (renderTime > 10) {
        UI_LOG_WARNING("[UI] renderPlaylistTree: {} ms ({} nœuds rendus) - PERFORMANCE CRITIQUE", renderTime, nodesRendered);
    }
    
    ImGui::EndChild();
}

void UIManager::renderPlaylistNavigation() {
    ImGui::Spacing();
    
    // Invalider le cache si nécessaire
    PlaylistNode* currentNode = m_playlist.getCurrentNode();
    if (!m_navigationCacheValid || 
        (currentNode && (m_cachedCurrentIndex < 0 || 
                         m_cachedCurrentIndex >= static_cast<int>(m_cachedAllFiles.size()) ||
                         m_cachedAllFiles[m_cachedCurrentIndex] != currentNode))) {
        // Reconstruire le cache (avec filtrage dynamique, utiliser uniquement l'arbre original)
        m_cachedAllFiles.clear();
        
        // Collecter tous les fichiers de l'arbre original (filtrage dynamique au rendu)
        std::function<void(PlaylistNode*)> collectFiles = [&](PlaylistNode* node) {
            if (!node) return;
            
            // Filtrer dynamiquement : ne collecter que les fichiers qui matchent
            if (!node->isFolder && !node->filepath.empty()) {
                if (!m_filtersActive || matchesFilters(node)) {
                    m_cachedAllFiles.push_back(node);
                }
            }
            
            // Parcourir les enfants (seulement si dossier ouvert ou pas de filtres)
            if (node->isFolder) {
                bool shouldTraverse = !m_filtersActive || hasVisibleChildren(node);
                if (shouldTraverse) {
                    for (auto& child : node->children) {
                        collectFiles(child.get());
                    }
                }
            }
        };
        
        PlaylistNode* root = m_playlist.getRoot();
        if (root) {
            for (auto& child : root->children) {
                collectFiles(child.get());
            }
        }
        
        // Trouver l'index du nœud courant dans la liste
        m_cachedCurrentIndex = -1;
        if (currentNode) {
            for (size_t i = 0; i < m_cachedAllFiles.size(); ++i) {
                if (m_cachedAllFiles[i] == currentNode || 
                    (!currentNode->filepath.empty() && 
                     m_cachedAllFiles[i]->filepath == currentNode->filepath)) {
                    m_cachedCurrentIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        
        m_navigationCacheValid = true;
    }
    
    // Utiliser le cache
    const std::vector<PlaylistNode*>& allFiles = m_cachedAllFiles;
    int currentIndex = m_cachedCurrentIndex;
    
    float navButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    
    if (ImGui::Button(ICON_FA_BACKWARD_STEP "", ImVec2(navButtonWidth, 0))) {
        if (currentIndex > 0 && currentIndex < static_cast<int>(allFiles.size())) {
            PlaylistNode* prev = allFiles[currentIndex - 1];
            // Trouver le nœud correspondant dans l'arbre original (ou filtré) pour setCurrentNode
            PlaylistNode* targetNode = prev;
            if (m_filtersActive) {
                // Si on est dans l'arbre filtré, trouver le nœud correspondant dans l'arbre original
                // pour que setCurrentNode fonctionne correctement
                PlaylistNode* originalNode = m_playlist.findNodeByPath(prev->filepath);
                if (originalNode) {
                    targetNode = originalNode;
                }
            }
            m_playlist.setCurrentNode(targetNode);
            if (m_player.loadFile(prev->filepath)) {
                m_player.play();
                recordHistoryEntry(prev->filepath);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP "", ImVec2(navButtonWidth, 0))) {
        if (currentIndex >= 0 && currentIndex < static_cast<int>(allFiles.size()) - 1) {
            PlaylistNode* next = allFiles[currentIndex + 1];
            // Trouver le nœud correspondant dans l'arbre original (ou filtré) pour setCurrentNode
            PlaylistNode* targetNode = next;
            if (m_filtersActive) {
                // Si on est dans l'arbre filtré, trouver le nœud correspondant dans l'arbre original
                // pour que setCurrentNode fonctionne correctement
                PlaylistNode* originalNode = m_playlist.findNodeByPath(next->filepath);
                if (originalNode) {
                    targetNode = originalNode;
                }
            }
            m_playlist.setCurrentNode(targetNode);
            if (m_player.loadFile(next->filepath)) {
                m_player.play();
                recordHistoryEntry(next->filepath);
            }
        }
    }
}

void UIManager::renderFileBrowser() {
    if (!m_showFileDialog) return;
    
    m_fileBrowser.render(m_showFileDialog, m_selectedFilePath);
    
    if (!m_selectedFilePath.empty() && fs::exists(m_selectedFilePath)) {
        if (m_player.loadFile(m_selectedFilePath)) {
            m_player.play();
            recordHistoryEntry(m_selectedFilePath);
            m_showFileDialog = false;
            m_selectedFilePath = "";
        }
    }
}

void UIManager::renderBackground() {
    if (!m_background.isShown()) return;
    
    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    m_background.render(windowWidth, windowHeight);
}

void UIManager::updateSearchResults() {
    m_searchResults.clear();
    
    if (m_searchQuery.empty()) {
        m_searchPending = false;
        return;
    }
    
    // Ne pas rechercher si la requête est trop courte (moins de 2 caractères)
    if (m_searchQuery.length() < 2) {
        m_searchPending = false;
        return;
    }
    
    // Rechercher dans la base de données
    auto results = m_database.search(m_searchQuery);
    
    // Limiter à 25 résultats maximum
    for (size_t i = 0; i < results.size() && i < 25; ++i) {
        m_searchResults.push_back(results[i]);
        // Mettre à jour le cache filepath -> hash
        if (!results[i]->filepath.empty() && results[i]->metadataHash != 0) {
            m_filepathToHashCache[results[i]->filepath] = results[i]->metadataHash;
        }
    }
    
    m_searchPending = false;
}

void UIManager::navigateToFile(const std::string& filepath) {
    if (filepath.empty()) return;
    
    // Trouver le nœud correspondant dans la playlist
    auto allFiles = m_playlist.getAllFiles();
    for (PlaylistNode* node : allFiles) {
        if (node && node->filepath == filepath) {
            // Si des filtres sont actifs et que le fichier ne correspond pas, désactiver les filtres
            // pour que le fichier soit visible dans l'arbre
            if (m_filtersActive && !matchesFilters(node)) {
                m_filterAuthor.clear();
                m_filterYear.clear();
                m_filterRating = 0;
                m_filtersActive = false;
                invalidateFlatList();
                invalidateVisibleIndices();
                // Invalider aussi le cache de navigation
                m_navigationCacheValid = false;
            }
            
            // Sélectionner ce nœud
            m_playlist.setCurrentNode(node);
            m_playlist.setScrollToCurrent(true);
            m_shouldFocusPlaylist = true;  // Donner le focus à la playlist à la prochaine frame
            
            // Charger et jouer le fichier
            if (m_player.loadFile(filepath)) {
                m_player.play();
                recordHistoryEntry(filepath);
            }
            
            // Effacer la recherche
            m_searchQuery.clear();
            m_searchResults.clear();
            m_selectedSearchResult = -1;
            m_searchListFocused = false;
            
            break;
        }
    }
}

void UIManager::setDatabaseOperationInProgress(bool inProgress, const std::string& status, float progress) {
    // Thread-safe : pas de mutex nécessaire car ces variables sont atomiques ou simples
    // et ne sont lues que depuis le thread UI
    m_databaseOperationInProgress = inProgress;
    m_databaseOperationStatus = status;
    m_databaseOperationProgress = progress;
}

void UIManager::recordHistoryEntry(const std::string& filepath) {
    if (filepath.empty()) return;
    
    // Récupérer les métadonnées du fichier
    const SidMetadata* metadata = m_database.getMetadata(filepath);
    if (!metadata) return; // Pas de métadonnées = pas d'historique
    
    // Enregistrer dans l'historique (append rapide)
    m_history.addEntry(metadata->title, metadata->author, metadata->metadataHash);
    
    // Incrémenter le playCount dans le RatingManager
    // Note: pas de sync automatique ici, le playCount sera sync à la sortie ou sur push manuel
    m_ratingManager.incrementPlayCount(metadata->metadataHash);
}

void UIManager::rebuildFilepathToHashCache() {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    m_filepathToHashCache.clear();
    
    // Parcourir tous les fichiers de la playlist et remplir le cache
    auto allFiles = m_playlist.getAllFiles();
    size_t cached = 0;
    
    for (PlaylistNode* node : allFiles) {
        if (!node || node->filepath.empty()) continue;
        
        const SidMetadata* metadata = m_database.getMetadata(node->filepath);
        if (metadata && metadata->metadataHash != 0) {
            m_filepathToHashCache[node->filepath] = metadata->metadataHash;
            cached++;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    // Log seulement si ça prend du temps (> 100ms) ou si beaucoup de fichiers
    if (totalTime > 100 || cached > 1000) {
        UI_LOG_INFO("[UI] rebuildFilepathToHashCache(): {} ms ({} fichiers mis en cache)", totalTime, cached);
    }
    
    // Marquer que les filtres doivent être mis à jour
    m_filtersNeedUpdate = true;
}

void UIManager::updateFilterLists() {
    if (!m_filtersNeedUpdate) return;
    
    m_availableAuthors.clear();
    m_availableYears.clear();
    // Pour les auteurs : extraire depuis la base de données
    std::unordered_set<std::string> authorsSet;
    
    // Parcourir tous les fichiers de la playlist
    auto allFiles = m_playlist.getAllFiles();
    for (PlaylistNode* node : allFiles) {
        if (!node || node->filepath.empty()) continue;
        
        const SidMetadata* metadata = m_database.getMetadata(node->filepath);
        if (metadata) {
            if (!metadata->author.empty()) {
                authorsSet.insert(metadata->author);
            }
        }
    }
    
    // Convertir en vecteur trié pour les auteurs
    m_availableAuthors.assign(authorsSet.begin(), authorsSet.end());
    std::sort(m_availableAuthors.begin(), m_availableAuthors.end());
    
    // Pour les années : générer une liste de 1980 à l'année courante (sans limite)
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* localTime = std::localtime(&time_t);
    int currentYear = localTime->tm_year + 1900;
    
    // S'assurer que l'année courante est au moins 2024 (au cas où localtime échoue)
    if (currentYear < 2024) {
        currentYear = 2024;
    }
    
    m_availableYears.clear();
    // Générer de 1980 à l'année courante (pas de limite)
    for (int year = 1980; year <= currentYear; ++year) {
        m_availableYears.push_back(std::to_string(year));
    }
    
    m_filtersNeedUpdate = false;
    
    // Initialiser les widgets de filtre avec les listes disponibles
    m_authorFilterWidget.initialize(m_availableAuthors);
    m_yearFilterWidget.initialize(m_availableYears);
    
    // Avec filtrage dynamique, pas besoin de reconstruire l'arbre
}

bool UIManager::matchesFilters(PlaylistNode* node) const {
    if (!node) return false;
    
    // Les dossiers passent toujours (on filtre seulement les fichiers)
    if (node->isFolder) {
        return true;
    }
    
    // Si aucun filtre n'est actif, tout passe
    if (m_filterAuthor.empty() && m_filterYear.empty() && m_filterRating == 0) {
        return true;
    }
    
    // Pour les fichiers, vérifier les métadonnées
    if (node->filepath.empty()) {
        return false; // Fichier sans chemin = ne matche pas
    }
    
    // Récupérer les métadonnées
    const SidMetadata* metadata = m_database.getMetadata(node->filepath);
    if (!metadata) {
        // Si pas de métadonnées, NE PAS afficher (fichier non indexé = exclu du filtre)
        return false;
    }
    
    bool authorMatches = true;
    bool yearMatches = true;
    bool ratingMatches = true;
    
    // Vérifier le filtre auteur (comparaison partielle, insensible à la casse)
    if (!m_filterAuthor.empty()) {
        // Comparaison partielle : l'auteur doit contenir le filtre (insensible à la casse)
        // IMPORTANT : on compare uniquement le champ author, pas le title ni le filename
        std::string authorLower = metadata->author;
        std::string filterLower = m_filterAuthor;
        std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(), ::tolower);
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
        
        authorMatches = (authorLower.find(filterLower) != std::string::npos);
        if (!authorMatches) {
            return false; // Auteur ne matche pas, pas besoin de vérifier le reste
        }
    }

    // Vérifier le filtre année
    // Le champ released peut contenir une date complète (ex: "1985", "1985-01-01", "1985/01/01")
    // On extrait l'année (les 4 premiers chiffres) pour la comparaison
    if (!m_filterYear.empty()) {
        if (metadata->released.empty()) {
            return false; // Pas de date = ne matche pas
        }
        
        // Extraire l'année du champ released (chercher les 4 premiers chiffres)
        std::string yearFromReleased;
        for (char c : metadata->released) {
            if (std::isdigit(c)) {
                yearFromReleased += c;
                if (yearFromReleased.length() == 4) {
                    break; // On a trouvé l'année (4 chiffres)
                }
            } else if (!yearFromReleased.empty()) {
                // Si on a déjà commencé à collecter des chiffres et on rencontre un non-chiffre,
                // on s'arrête (cas où la date est "1985-01-01" ou "1985/01/01")
                if (yearFromReleased.length() == 4) {
                    break;
                }
                yearFromReleased.clear(); // Réinitialiser si on n'a pas encore 4 chiffres
            }
        }
        
        // Comparaison de l'année : l'année extraite doit correspondre exactement ou commencer par le filtre
        // Cela permet de filtrer avec "198" pour trouver "1985", "1986", etc.
        // Mais on compare uniquement le début de l'année pour éviter les faux positifs
        yearMatches = (yearFromReleased.length() >= m_filterYear.length() && 
                      yearFromReleased.substr(0, m_filterYear.length()) == m_filterYear);
        if (!yearMatches) {
            return false; // Année ne matche pas
        }
    }
    
    // Vérifier le filtre rating
    if (m_filterRating > 0) {
        // Récupérer le rating depuis RatingManager
        int fileRating = m_ratingManager.getRating(metadata->metadataHash);
        
        if (m_filterRatingOperator) {
            // Opérateur >= : le rating du fichier doit être >= au filtre
            ratingMatches = (fileRating >= m_filterRating);
        } else {
            // Opérateur = : le rating du fichier doit être exactement égal au filtre
            ratingMatches = (fileRating == m_filterRating);
        }
        
        if (!ratingMatches) {
            return false; // Rating ne matche pas
        }
    }
    
    // Tous les filtres matchent (si actifs)
    return true;
}

bool UIManager::hasVisibleChildren(PlaylistNode* node) const {
    if (!node || !node->isFolder) return false;
    if (!m_filtersActive) return true;  // Si pas de filtres, tous les dossiers sont visibles
    
    // Parcourir récursivement pour trouver un enfant visible
    std::function<bool(PlaylistNode*)> hasVisible = [&](PlaylistNode* n) -> bool {
        if (!n) return false;
        
        if (n->isFolder) {
            // Pour un dossier : vérifier récursivement ses enfants
            for (auto& child : n->children) {
                if (hasVisible(child.get())) {
                    return true;
                }
            }
        } else {
            // Pour un fichier : vérifier directement le filtre
            return matchesFilters(n);
        }
        
        return false;
    };
    
    for (auto& child : node->children) {
        if (hasVisible(child.get())) {
            return true;
        }
    }
    
    return false;
}

void UIManager::expandAllNodes() {
    PlaylistNode* root = m_playlist.getRoot();
    if (!root) return;
    
    // Parcourir récursivement TOUS les nœuds de l'arbre (pas seulement ceux visibles)
    // pour ouvrir tous les dossiers, même si la racine est repliée
    std::function<void(PlaylistNode*)> expand = [&](PlaylistNode* node) {
        if (!node) return;
        
        if (node->isFolder) {
            m_openNodes[node] = true;
            // Continuer récursivement avec tous les enfants (même si le parent n'est pas encore ouvert)
            for (auto& child : node->children) {
                expand(child.get());
            }
        }
    };
    
    // Ouvrir tous les enfants de la racine (même si la racine est "repliée")
    for (auto& child : root->children) {
        expand(child.get());
    }
    
    // Invalider et reconstruire immédiatement pour que le changement soit visible
    invalidateFlatList();
    buildFlatList();  // Reconstruire immédiatement
    if (m_filtersActive) {
        invalidateVisibleIndices();
        buildVisibleIndices();  // Reconstruire aussi les indices si filtres actifs
    }
}

void UIManager::collapseAllNodes() {
    PlaylistNode* root = m_playlist.getRoot();
    if (!root) return;
    
    // Parcourir récursivement TOUS les nœuds de l'arbre pour les marquer explicitement comme fermés
    // Cela garantit que même les nœuds non visibles sont marqués comme fermés
    std::function<void(PlaylistNode*)> collapse = [&](PlaylistNode* node) {
        if (!node) return;
        
        if (node->isFolder) {
            m_openNodes[node] = false;  // Marquer explicitement comme fermé
            // Continuer récursivement avec tous les enfants
            for (auto& child : node->children) {
                collapse(child.get());
            }
        }
    };
    
    // Fermer tous les enfants de la racine
    for (auto& child : root->children) {
        collapse(child.get());
    }
    
    // Invalider et reconstruire immédiatement pour que le changement soit visible
    invalidateFlatList();
    buildFlatList();  // Reconstruire immédiatement
    if (m_filtersActive) {
        invalidateVisibleIndices();
        buildVisibleIndices();  // Reconstruire aussi les indices si filtres actifs
    }
}

void UIManager::buildFlatList() {
    if (m_flatListValid) return;
    
    m_flatList.clear();
    PlaylistNode* root = m_playlist.getRoot();
    if (!root) {
        m_flatListValid = true;
        return;
    }
    
    // Parcourir récursivement l'arbre et construire la liste plate
    // Seulement les nœuds dont tous les parents sont ouverts
    std::function<void(PlaylistNode*, int)> flatten = [&](PlaylistNode* node, int depth) {
        if (!node) return;
        
        // Vérifier si tous les parents sont ouverts (sauf la racine qui est toujours visible)
        bool isVisible = true;
        PlaylistNode* parent = node->parent;
        while (parent && parent != root) {
            if (m_openNodes.find(parent) == m_openNodes.end() || !m_openNodes[parent]) {
                isVisible = false;
                break;
            }
            parent = parent->parent;
        }
        
        // Ajouter seulement si visible
        if (isVisible) {
            FlatNode flatNode;
            flatNode.node = node;
            flatNode.depth = depth;
            flatNode.index = m_flatList.size();
            m_flatList.push_back(flatNode);
            
            // Si c'est un dossier ouvert, continuer avec les enfants
            if (node->isFolder) {
                bool isOpen = m_openNodes.find(node) != m_openNodes.end() && m_openNodes[node];
                if (isOpen) {
                    for (auto& child : node->children) {
                        flatten(child.get(), depth + 1);
                    }
                }
            }
        }
    };
    
    // Parcourir les enfants de la racine (toujours visibles)
    for (auto& child : root->children) {
        flatten(child.get(), 0);
    }
    
    m_flatListValid = true;
}

void UIManager::buildVisibleIndices() {
    if (m_visibleIndicesValid) return;
    if (!m_flatListValid) {
        buildFlatList();
    }
    
    m_visibleIndices.clear();
    
    // Filtrer la liste plate : ne garder que les indices des nœuds visibles
    for (size_t i = 0; i < m_flatList.size(); i++) {
        const FlatNode& flatNode = m_flatList[i];
        PlaylistNode* node = flatNode.node;
        if (!node) continue;
        
        bool shouldShow = true;
        if (node->isFolder) {
            // Pour un dossier : vérifier s'il a des enfants visibles
            shouldShow = hasVisibleChildren(node);
        } else {
            // Pour un fichier : vérifier directement le filtre
            shouldShow = matchesFilters(node);
        }
        
        if (shouldShow) {
            m_visibleIndices.push_back(i);
        }
    }
    
    m_visibleIndicesValid = true;
}

void UIManager::invalidateFlatList() {
    m_flatListValid = false;
    invalidateVisibleIndices();  // Invalider aussi la liste d'indices
}

void UIManager::invalidateVisibleIndices() {
    m_visibleIndicesValid = false;
}

void UIManager::invalidateNavigationCache() {
    m_navigationCacheValid = false;
    m_cachedAllFiles.clear();
    m_cachedCurrentIndex = -1;
}

PlaylistNode* UIManager::getNextFilteredFile() {
    // Utiliser uniquement le cache existant (construit par renderPlaylistNavigation)
    // Le cache est construit/mis à jour par renderPlaylistNavigation() à chaque frame
    if (!m_navigationCacheValid || m_cachedAllFiles.empty()) {
        return nullptr;
    }
    
    // Utiliser le cache pour trouver le prochain fichier
    const std::vector<PlaylistNode*>& allFiles = m_cachedAllFiles;
    int currentIndex = m_cachedCurrentIndex;
    
    if (currentIndex >= 0 && currentIndex < static_cast<int>(allFiles.size()) - 1) {
        return allFiles[currentIndex + 1];
    } else if (!allFiles.empty()) {
        // Reboucler au début si on est à la fin
        return allFiles[0];
    }
    
    return nullptr;
}

void UIManager::refreshPlaylistTree() {
    invalidateFlatList();
    invalidateVisibleIndices();
    invalidateNavigationCache();
}

void UIManager::renderFilters() {
    // Mettre à jour les listes si nécessaire
    if (m_filtersNeedUpdate) {
        updateFilterLists();
    }
    
    ImGui::Text("Filters:");
    
    // Détecter la flèche bas depuis les champs de recherche
    bool downArrowPressed = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    
    // Vérifier si les filtres étaient actifs avant le rendu
    bool previousFiltersActive = m_filtersActive;
    
    // Utiliser des largeurs fixes pour éviter que les filtres s'élargissent
    // et poussent les autres éléments hors de la vue
    float authorWidth = 200.0f;
    float yearWidth = 150.0f;
    float ratingComboWidth = 120.0f;  // Élargi pour afficher "All Stars" en entier
    float ratingButtonWidth = 40.0f;
    
    // Filtre Auteur
    // Mode 1 : Recherche (focus) - Mode 2 : Sélection (arbre filtré)
    UI_LOG_DEBUG("[Filter] renderFilters: Auteur='{}', Année='{}'", m_filterAuthor, m_filterYear);
    ImGui::PushItemWidth(authorWidth);  // Forcer la largeur du filtre auteur
    bool authorChanged = m_authorFilterWidget.render(m_filterAuthor, m_availableAuthors, 
        [this](const std::string& value) {
            // IMPORTANT : Préserver m_filterYear lors du changement de m_filterAuthor
            std::string previousYear = m_filterYear;
            std::string previousAuthor = m_filterAuthor;
            m_filterAuthor = value;
            // S'assurer que m_filterYear n'a pas été modifié
            if (m_filterYear != previousYear) {
                UI_LOG_WARNING("[Filter] m_filterYear modifié lors du changement d'auteur! Restauration: '{}' -> '{}'", previousYear, m_filterYear);
                m_filterYear = previousYear;
            }
            // Mode 2 : Quand on sélectionne un item, activer le filtre (filtrage dynamique)
            m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty() || m_filterRating > 0;
            UI_LOG_DEBUG("[Filter] Auteur changé: '{}' -> '{}', Année préservée: '{}', Filtres actifs: {}", 
                     previousAuthor, m_filterAuthor, m_filterYear, m_filtersActive);
            // Invalider la liste filtrée pour qu'elle soit reconstruite avec les nouveaux filtres
            invalidateVisibleIndices();  // Forcer le rafraîchissement de l'affichage
        },
        downArrowPressed);
    ImGui::PopItemWidth();
    
    // Filtre Année (label intégré dans le hint : "All Year")
    ImGui::SameLine(0, 20);  // Espacement
    ImGui::PushItemWidth(yearWidth);  // Forcer la largeur du filtre année
    bool yearChanged = m_yearFilterWidget.render(m_filterYear, m_availableYears,
        [this](const std::string& value) {
            // IMPORTANT : Préserver m_filterAuthor lors du changement de m_filterYear
            std::string previousAuthor = m_filterAuthor;
            std::string previousYear = m_filterYear;
            m_filterYear = value;
            // S'assurer que m_filterAuthor n'a pas été modifié
            if (m_filterAuthor != previousAuthor) {
                UI_LOG_WARNING("[Filter] m_filterAuthor modifié lors du changement d'année! Restauration: '{}' -> '{}'", previousAuthor, m_filterAuthor);
                m_filterAuthor = previousAuthor;
            }
            // Mode 2 : Quand on sélectionne un item, activer le filtre (filtrage dynamique)
            m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty() || m_filterRating > 0;
            UI_LOG_DEBUG("[Filter] Année changée: '{}' -> '{}', Auteur préservé: '{}', Filtres actifs: {}", 
                     previousYear, m_filterYear, m_filterAuthor, m_filtersActive);
            // Invalider la liste filtrée pour qu'elle soit reconstruite avec les nouveaux filtres
            invalidateVisibleIndices();  // Forcer le rafraîchissement de l'affichage
        },
        downArrowPressed);
    ImGui::PopItemWidth();
    
    // Filtre Rating
    ImGui::SameLine(0, 20);  // Espacement
    
    // Combo box pour sélectionner le nombre d'étoiles (5, 4, 3, 2, 1)
    // Label intégré : "All Stars" au lieu de "All"
    const char* ratingItems[] = { "All Stars", "5", "4", "3", "2", "1" };
    // Convertir m_filterRating (0-5) en index combo box (0 = All, 1 = "5", 2 = "4", etc.)
    int currentRatingIndex = (m_filterRating == 0) ? 0 : (6 - m_filterRating);
    ImGui::PushItemWidth(ratingComboWidth);  // Forcer la largeur du combo rating
    if (ImGui::Combo("##rating", &currentRatingIndex, ratingItems, 6)) {
        // Convertir l'index combo box en valeur rating (0 = All, 1-5 = nombre d'étoiles)
        m_filterRating = (currentRatingIndex == 0) ? 0 : (6 - currentRatingIndex);
        m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty() || m_filterRating > 0;
        invalidateVisibleIndices();
        UI_LOG_DEBUG("[Filter] Rating changé: {} (index combo: {})", m_filterRating, currentRatingIndex);
    }
    
    ImGui::PopItemWidth();
    
    // Bouton opérateur (>= ou =) - seulement si un rating est sélectionné
    if (m_filterRating > 0) {
        ImGui::SameLine();
        const char* operatorLabel = m_filterRatingOperator ? ">=" : "=";
        if (ImGui::Button(operatorLabel, ImVec2(ratingButtonWidth, 0))) {
            m_filterRatingOperator = !m_filterRatingOperator;
            invalidateVisibleIndices();
            UI_LOG_DEBUG("[Filter] Opérateur rating changé: {}", m_filterRatingOperator ? ">=" : "=");
        }
    }
    
    // Si les filtres ont changé, invalider les indices visibles pour forcer le rafraîchissement
    if (authorChanged || yearChanged || previousFiltersActive != m_filtersActive) {
        invalidateVisibleIndices();
        UI_LOG_DEBUG("[Filter] Filtres modifiés, invalidation des indices visibles");
    }
}

void UIManager::renderExplorerTab() {
    auto tabStart = std::chrono::high_resolution_clock::now();
    
    // Champ de recherche fuzzy
    ImGui::Text("Search:");
    ImGui::SameLine();
    
    // Griser le champ si une opération de DB est en cours
    bool dbOperationInProgress = isDatabaseOperationInProgress();
    if (dbOperationInProgress) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    
    char searchBuffer[256];
    strncpy(searchBuffer, m_searchQuery.c_str(), sizeof(searchBuffer) - 1);
    searchBuffer[sizeof(searchBuffer) - 1] = '\0';
    
    // Utiliser EnterReturnsTrue pour détecter quand Entrée est pressée
    bool enterPressed = ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer), 
                                         (dbOperationInProgress ? ImGuiInputTextFlags_ReadOnly : 0) | ImGuiInputTextFlags_EnterReturnsTrue);
    
    bool textChanged = false;
    
    // Si Entrée est pressée et qu'il y a des résultats, naviguer vers le premier résultat
    if (enterPressed && !m_searchResults.empty()) {
        navigateToFile(m_searchResults[0]->filepath);
        // Ne pas traiter textChanged car on a déjà navigué
        textChanged = false;
    } else {
        // Détecter les changements de texte (sauf si Entrée a été pressée et qu'on a navigué)
        textChanged = (strcmp(searchBuffer, m_searchQuery.c_str()) != 0);
    }
    
    if (dbOperationInProgress) {
        ImGui::PopStyleVar();
        
        // Afficher un indicateur de progression
        float progress = getDatabaseOperationProgress();
        std::string status = getDatabaseOperationStatus();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " %s", status.c_str());
        if (progress > 0.0f && progress < 1.0f) {
            ImGui::ProgressBar(progress, ImVec2(0, 0), "");
        }
    }
    
    // Gérer la navigation clavier : flèche bas depuis le champ de recherche
    bool downArrowFromSearch = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    if (downArrowFromSearch && !m_searchListFocused) {
        if (!m_searchResults.empty()) {
            m_searchListFocused = true;
            m_selectedSearchResult = 0; // Forcer le premier élément (index 0)
        }
    }
    
    if (textChanged) {
        m_selectedSearchResult = -1;
        m_searchListFocused = false; // Reset focus sur liste quand on tape
        // Debounce : ne pas rechercher immédiatement, mais marquer qu'une recherche est en attente
        m_pendingSearchQuery = searchBuffer;
        m_lastSearchInputTime = std::chrono::high_resolution_clock::now();
        m_searchPending = true;
    }
    
    // Gérer le debounce : lancer la recherche après 300ms d'inactivité
    if (m_searchPending) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSearchInputTime).count();
        
        if (elapsed >= 300) {  // 300ms de délai
            m_searchQuery = m_pendingSearchQuery;
            updateSearchResults();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "##clearsearch")) {
        m_searchQuery.clear();
        m_pendingSearchQuery.clear();
        m_searchResults.clear();
        m_selectedSearchResult = -1;
        m_searchListFocused = false;
        m_searchPending = false;
    }
    
    // Afficher les résultats dans une zone dédiée sous le champ de recherche
    if (!m_searchQuery.empty() && !m_searchResults.empty()) {
        ImGui::Spacing();
        // Calculer la hauteur avec un minimum (comme pour FilterWidget)
        float calculatedHeight = m_searchResults.size() * 25.0f;
        float minHeight = 100.0f; // Hauteur minimale pour garantir la cliquabilité
        float maxHeight = 200.0f; // Hauteur maximale raisonnable
        float listHeight = std::max(minHeight, std::min(calculatedHeight, maxHeight));
        ImGui::BeginChild("##searchResultsList", ImVec2(0, listHeight), true);
        
        // Gérer la navigation clavier dans la liste
        // Ne traiter la flèche bas que si elle n'a pas déjà été traitée dans le champ de recherche
        if (m_searchListFocused && !downArrowFromSearch) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                if (m_selectedSearchResult > 0) {
                    m_selectedSearchResult--;
                } else {
                    // Retour au champ de recherche
                    m_searchListFocused = false;
                    m_selectedSearchResult = -1;
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                if (m_selectedSearchResult < (int)m_searchResults.size() - 1) {
                    m_selectedSearchResult++;
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                if (m_selectedSearchResult >= 0 && m_selectedSearchResult < (int)m_searchResults.size()) {
                    navigateToFile(m_searchResults[m_selectedSearchResult]->filepath);
                }
            }
        }
        
        for (size_t i = 0; i < m_searchResults.size() && i < 25; ++i) {
            const SidMetadata* metadata = m_searchResults[i];
            std::string label = metadata->title;
            if (!metadata->author.empty()) {
                label += " - " + metadata->author;
            }
            if (!metadata->released.empty()) {
                label += " (" + metadata->released + ")";
            }
            
            bool isSelected = (m_selectedSearchResult == (int)i && m_searchListFocused);
            
            // Mettre en évidence le résultat sélectionné
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 0.5f));
            }
            
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                // Naviguer vers ce fichier dans l'arbre
                navigateToFile(metadata->filepath);
            }
            
            if (isSelected) {
                ImGui::PopStyleColor(2);
                ImGui::SetScrollHereY(0.5f); // Scroller vers l'élément sélectionné
            }
        }
        
        ImGui::EndChild();
    } else {
        m_searchListFocused = false;
    }
    
    ImGui::Separator();
    
    // Boutons Clear et Index sur la même ligne
    float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    
    if (ImGui::Button(ICON_FA_TRASH " Clear", ImVec2(buttonWidth, 0))) {
        m_playlist.clear();
        m_playlist.setCurrentNode(nullptr);
        m_database.clear();  // Supprimer la base de données en mémoire et sur disque
        invalidateNavigationCache();
        invalidateFlatList();  // Invalider la liste plate car la playlist change
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_PLUS " Index", ImVec2(buttonWidth, 0))) {
        // L'indexation sera gérée par Application
        m_indexRequested = true;
    }
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // Filtres multicritères
    renderFilters();
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // Bouton Expand/Collapse tous les nœuds
    PlaylistNode* root = m_playlist.getRoot();
    bool allExpanded = true;
    if (root && !root->children.empty()) {
        // Vérifier si tous les dossiers sont ouverts (en utilisant la liste plate si disponible)
        if (m_flatListValid) {
            // Utiliser la liste plate pour vérifier plus efficacement
            for (const FlatNode& flatNode : m_flatList) {
                PlaylistNode* node = flatNode.node;
                if (node && node->isFolder) {
                    if (m_openNodes.find(node) == m_openNodes.end() || !m_openNodes[node]) {
                        allExpanded = false;
                        break;
                    }
                }
            }
        } else {
            // Fallback : vérifier récursivement
            std::function<void(PlaylistNode*)> checkExpanded = [&](PlaylistNode* node) {
                if (!node || !allExpanded) return;
                if (node->isFolder) {
                    if (m_openNodes.find(node) == m_openNodes.end() || !m_openNodes[node]) {
                        allExpanded = false;
                        return;
                    }
                    for (auto& child : node->children) {
                        checkExpanded(child.get());
                    }
                }
            };
            for (auto& child : root->children) {
                checkExpanded(child.get());
                if (!allExpanded) break;
            }
        }
    }
    
    if (ImGui::Button(allExpanded ? ICON_FA_ANGLE_UP " Collapse All" : ICON_FA_ANGLE_DOWN " Expand All", ImVec2(-1, 0))) {
        if (allExpanded) {
            collapseAllNodes();
        } else {
            expandAllNodes();
        }
        // Les fonctions expandAllNodes/collapseAllNodes reconstruisent déjà les listes immédiatement
        // Forcer la reconstruction immédiate pour que le changement soit visible
        if (!m_flatListValid) {
            buildFlatList();
        }
        if (m_filtersActive && !m_visibleIndicesValid) {
            buildVisibleIndices();
        }
    }
    
    ImGui::Spacing();
    
    // Afficher "Drag & drop" dans le tree view si vide
    bool isEmpty = !root || root->children.empty();
    
    if (isEmpty) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Drag & drop .sid files or folders here");
        ImGui::Spacing();
    }
    
    auto t0 = std::chrono::high_resolution_clock::now();
    renderPlaylistTree();
    auto t1 = std::chrono::high_resolution_clock::now();
    static long long treeTime = 0;
    treeTime += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    
    auto t2 = std::chrono::high_resolution_clock::now();
    renderPlaylistNavigation();
    auto t3 = std::chrono::high_resolution_clock::now();
    static long long navTime = 0;
    navTime += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    
    auto tabEnd = std::chrono::high_resolution_clock::now();
    static long long explorerTabTime = 0;
    explorerTabTime += std::chrono::duration_cast<std::chrono::microseconds>(tabEnd - tabStart).count();
    
    // Afficher les timings accumulés toutes les 10 frames (pour capture de référence)
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 10 == 0) {
        UI_LOG_DEBUG("[ExplorerTab] Timings (frame {}): Tree={:.2f} ms/frame avg, Nav={:.2f} ms/frame avg, Total={:.2f} ms/frame avg",
                  frameCount, treeTime / 10.0 / 1000.0, navTime / 10.0 / 1000.0, explorerTabTime / 10.0 / 1000.0);
        explorerTabTime = 0;
        treeTime = 0;
        navTime = 0;
    }
}

void UIManager::renderHistoryTab() {
    const auto& originalEntries = m_history.getEntries();
    
    ImGui::Text("History (%zu entries)", originalEntries.size());
    ImGui::Separator();
    
    ImGui::BeginChild("HistoryList", ImVec2(0, 0), true);
    
    // Créer une copie pour le tri (on ne peut pas modifier le vecteur const)
    static std::vector<HistoryEntry> sortedEntries;
    static bool entriesDirty = true;
    
    // Recréer la copie si nécessaire
    if (entriesDirty || sortedEntries.size() != originalEntries.size()) {
        sortedEntries = originalEntries;
        entriesDirty = false;
    }
    
    // Créer un tableau avec colonnes triables et redimensionnables
    if (ImGui::BeginTable("HistoryTable", 5, 
                          ImGuiTableFlags_Borders | 
                          ImGuiTableFlags_RowBg | 
                          ImGuiTableFlags_ScrollY | 
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_SortMulti |
                          ImGuiTableFlags_Resizable,
                          ImVec2(0, 0))) {
        
        // En-têtes de colonnes avec tri (Date/Heure en dernier, moins important)
        // Les colonnes sont redimensionnables par défaut (pas besoin de flag spécial)
        ImGui::TableSetupColumn("Titre", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending);
        ImGui::TableSetupColumn("Auteur", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending);
        ImGui::TableSetupColumn("Plays", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 60.0f);
        ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80.0f);
        ImGui::TableSetupColumn("Date/Heure", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 144.0f);
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableHeadersRow();
        
        // Trier les entrées si nécessaire
        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsCount > 0 && sortSpecs->SpecsDirty) {
                // Recréer la copie avant de trier
                sortedEntries = originalEntries;
                
                std::sort(sortedEntries.begin(), sortedEntries.end(), [this, &sortSpecs](const HistoryEntry& a, const HistoryEntry& b) {
                    for (int n = 0; n < sortSpecs->SpecsCount; n++) {
                        const ImGuiTableColumnSortSpecs* sortSpec = &sortSpecs->Specs[n];
                        int delta = 0;
                        
                        switch (sortSpec->ColumnIndex) {
                            case 0: // Titre
                                delta = a.title.compare(b.title);
                                break;
                            case 1: // Auteur
                                delta = a.author.compare(b.author);
                                break;
                            case 2: // Plays
                                delta = (int)m_ratingManager.getPlayCount(a.metadataHash) - (int)m_ratingManager.getPlayCount(b.metadataHash);
                                break;
                            case 3: // Rating
                                // Récupérer les ratings depuis RatingManager
                                delta = m_ratingManager.getRating(a.metadataHash) - m_ratingManager.getRating(b.metadataHash);
                                break;
                            case 4: // Date/Heure
                                delta = a.timestamp.compare(b.timestamp);
                                break;
                        }
                        
                        if (delta > 0)
                            return (sortSpec->SortDirection == ImGuiSortDirection_Ascending) ? false : true;
                        if (delta < 0)
                            return (sortSpec->SortDirection == ImGuiSortDirection_Ascending) ? true : false;
                    }
                    return false;
                });
                sortSpecs->SpecsDirty = false;
            }
        }
        
        // Afficher les entrées
        for (const auto& entry : sortedEntries) {
            ImGui::TableNextRow();
            
            // Colonne Titre
            ImGui::TableSetColumnIndex(0);
            std::string title = entry.title.empty() ? "(Sans titre)" : entry.title;
            if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                // Trouver le fichier correspondant dans la playlist par metadataHash
                auto startTime = std::chrono::high_resolution_clock::now();
                auto allFiles = m_playlist.getAllFiles();
                UI_LOG_DEBUG("Looking for history entry: hash={}, title='{}', author='{}', total files in playlist: {}", 
                          entry.metadataHash, entry.title, entry.author, allFiles.size());
                
                bool found = false;
                int checkedCount = 0;
                int indexedCount = 0;
                int hashMatchCount = 0;
                
                for (PlaylistNode* node : allFiles) {
                    if (!node || node->filepath.empty()) continue;
                    checkedCount++;
                    
                    const SidMetadata* metadata = m_database.getMetadata(node->filepath);
                    if (metadata) {
                        indexedCount++;
                        if (metadata->metadataHash == entry.metadataHash) {
                            hashMatchCount++;
                            UI_LOG_DEBUG("Found matching file: {} (hash: {})", node->filepath, metadata->metadataHash);
                            
                            // Si des filtres sont actifs et que le fichier ne correspond pas, désactiver les filtres
                            // pour que le fichier soit visible dans l'arbre
                            if (m_filtersActive && !matchesFilters(node)) {
                                m_filterAuthor.clear();
                                m_filterYear.clear();
                                m_filtersActive = false;
                                invalidateFlatList();
                                invalidateVisibleIndices();
                                // Invalider aussi le cache de navigation
                                m_navigationCacheValid = false;
                            }
                            
                            // Sélectionner ce nœud
                            m_playlist.setCurrentNode(node);
                            m_playlist.setScrollToCurrent(true);
                            m_shouldFocusPlaylist = true;  // Donner le focus à la playlist à la prochaine frame
                            
                            // Charger et jouer le fichier
                            if (m_player.loadFile(node->filepath)) {
                                m_player.play();
                                recordHistoryEntry(node->filepath);
                                found = true;
                            }
                            break;
                        }
                    } else {
                        UI_LOG_DEBUG("File not indexed in database: {}", node->filepath);
                    }
                }
                
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                if (!found) {
                    UI_LOG_WARNING("History entry not found: checked {} files, {} indexed, {} hash matches, search took {} ms", 
                               checkedCount, indexedCount, hashMatchCount, duration.count());
                } else {
                    UI_LOG_DEBUG("History entry found in {} ms (checked {} files, {} indexed)", 
                             duration.count(), checkedCount, indexedCount);
                }
            }
            
            // Colonne Auteur
            ImGui::TableSetColumnIndex(1);
            std::string author = entry.author.empty() ? "(Inconnu)" : entry.author;
            ImGui::Text("%s", author.c_str());
            
            // Colonne Nombre de plays
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", m_ratingManager.getPlayCount(entry.metadataHash));
            
            // Colonne Rating
            ImGui::TableSetColumnIndex(3);
            // Récupérer le rating depuis RatingManager au lieu de entry.rating
            int rating = m_ratingManager.getRating(entry.metadataHash);
            // Réduire la taille des étoiles dans l'historique en utilisant PushFont avec taille réduite
            ImFont* currentFont = ImGui::GetFont();
            float originalFontSize = ImGui::GetFontSize();
            float reducedFontSize = originalFontSize * 0.7f; // Réduire à 70%
            ImGui::PushFont(currentFont, reducedFontSize);
            
            // Vérifier si le cyclage arc-en-ciel est activé
            Config& config = Config::getInstance();
            bool rainbowEnabled = config.isStarRatingRainbow();
            
            // Afficher les étoiles
            for (int i = 1; i <= 5; i++) {
                if (i <= rating) {
                    if (rainbowEnabled) {
                        // Calculer l'index dans la palette pour cette étoile
                        // Utiliser l'écart configurable entre les étoiles
                        int step = config.getStarRatingRainbowStep();
                        int offset = config.getStarRatingRainbowOffset();
                        int paletteIndex = ((i - 1) * step + m_rainbowCycleOffset + offset) % 255;
                        ImGui::PushStyleColor(ImGuiCol_Text, m_rainbowPalette[paletteIndex]);
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
                    }
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                }
                ImGui::Text("%s", ICON_FA_STAR);
                ImGui::PopStyleColor();
                if (i < 5) ImGui::SameLine(0.0f, 1.0f);
            }
            ImGui::PopFont(); // Restaurer la taille normale
            
            // Colonne Date/Heure (en dernier, moins important)
            ImGui::TableSetColumnIndex(4);
            // Formater le timestamp pour un affichage plus lisible
            // Format ISO: "2024-01-15T14:30:45" -> "2024-01-15 14:30"
            std::string displayTime = entry.timestamp;
            if (displayTime.length() >= 16) {
                // Remplacer 'T' par un espace et supprimer les secondes
                displayTime[10] = ' ';
                if (displayTime.length() > 16) {
                    displayTime = displayTime.substr(0, 16);
                }
            }
            ImGui::Text("%s", displayTime.c_str());
        }
        
        ImGui::EndTable();
    }
    
    ImGui::EndChild();
}

bool UIManager::renderStarRating(const char* label, int* rating, int max_stars) {
    bool changed = false;
    ImGui::PushID(label);

    // Taille ajustée : étoiles un peu plus petites, boutons un peu plus grands pour faciliter le clic
    float starSize = ImGui::GetTextLineHeight() * 0.85f;
    ImVec2 button_size = ImVec2(starSize * 2.0f, starSize * 1.3f);

    // Désactiver le highlight au survol pour tous les boutons de rating
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent au survol
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent au clic

    // Vérifier si le cyclage arc-en-ciel est activé
    Config& config = Config::getInstance();
    bool rainbowEnabled = config.isStarRatingRainbow();

    for (int i = 1; i <= max_stars; i++) {
        ImGui::PushID(i); // ID unique pour chaque étoile
        
        // Appliquer une couleur aux étoiles
        if (i <= *rating) {
            if (rainbowEnabled) {
                // Calculer l'index dans la palette pour cette étoile
                // Utiliser l'écart configurable entre les étoiles
                int step = config.getStarRatingRainbowStep();
                int offset = config.getStarRatingRainbowOffset();
                int paletteIndex = ((i - 1) * step + m_rainbowCycleOffset + offset) % 255;
                ImGui::PushStyleColor(ImGuiCol_Text, m_rainbowPalette[paletteIndex]);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f)); // Orange vif pour les étoiles pleines
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // Gris transparent pour les vides
        }

        // Espacement entre les étoiles
        if (i > 1) {
            ImGui::SameLine(0.0f, 8.0f);
        }

        // Utiliser Selectable avec un ID unique pour chaque étoile
        std::string starId = std::string(ICON_FA_STAR) + "##star" + std::to_string(i);
        
        if (ImGui::Selectable(starId.c_str(), false, 0, button_size)) {
            // Clic gauche : définir le rating
            *rating = i;
            changed = true;
        }
        
        // Clic droit sur une étoile : effacer le rating
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            *rating = 0;
            changed = true;
        }
        
        // Changer le curseur au survol
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        
        ImGui::PopStyleColor(); // Retirer la couleur de texte après chaque étoile
        ImGui::PopID(); // Retirer l'ID de l'étoile

        //if (ImGui::IsItemHovered()) {
        //    ImGui::SetTooltip("Donner %d etoiles", i);
        //}
    }

    // Retirer les styles de highlight
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return changed;
}

void UIManager::generateRainbowPalette() {
    // Générer une palette de 255 couleurs arc-en-ciel (HSV: H varie de 0 à 360, S=1, V=1)
    m_rainbowPalette.resize(255);
    
    for (int i = 0; i < 255; i++) {
        float hue = (i * 360.0f) / 255.0f; // H varie de 0 à 360
        float saturation = 1.0f;
        float value = 1.0f;
        
        // Conversion HSV vers RGB
        float c = value * saturation;
        float x = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
        float m = value - c;
        
        float r = 0.0f, g = 0.0f, b = 0.0f;
        
        if (hue < 60.0f) {
            r = c; g = x; b = 0.0f;
        } else if (hue < 120.0f) {
            r = x; g = c; b = 0.0f;
        } else if (hue < 180.0f) {
            r = 0.0f; g = c; b = x;
        } else if (hue < 240.0f) {
            r = 0.0f; g = x; b = c;
        } else if (hue < 300.0f) {
            r = x; g = 0.0f; b = c;
        } else {
            r = c; g = 0.0f; b = x;
        }
        
        m_rainbowPalette[i] = ImVec4(r + m, g + m, b + m, 1.0f);
    }
}

void UIManager::renderDebugWindow(long long newFrameTime, long long mainPanelTime, long long playlistPanelTime,
                                  long long fileBrowserTime, long long imguiRenderTime, long long clearTime,
                                  long long backgroundTime, long long renderDrawDataTime, long long presentTime, long long totalFrameTime) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Debug Window (Alt to toggle)", nullptr, ImGuiWindowFlags_None)) {
        // FPS
        ImGui::Text("FPS: %.1f", m_currentFPS);
        ImGui::Separator();
        
        // Node counts
        ImGui::Text("Playlist Tree:");
        ImGui::Text("  Total nodes: %zu", m_flatList.size());
        ImGui::Text("  Visible nodes: %zu", m_filtersActive ? m_visibleIndices.size() : m_flatList.size());
        ImGui::Text("  Filters: %s", m_filtersActive ? "Active" : "Inactive");
        ImGui::Separator();
        
        // Render timings
        ImGui::Text("Render Timings (microseconds):");
        if (totalFrameTime > 0) {
            ImGui::Text("  NewFrame:        %6.2f us (%.2f%%)", newFrameTime / 1000.0, (newFrameTime * 100.0) / totalFrameTime);
            ImGui::Text("  MainPanel:       %6.2f us (%.2f%%)", mainPanelTime / 1000.0, (mainPanelTime * 100.0) / totalFrameTime);
            ImGui::Text("  PlaylistPanel:   %6.2f us (%.2f%%)", playlistPanelTime / 1000.0, (playlistPanelTime * 100.0) / totalFrameTime);
            ImGui::Text("  FileBrowser:     %6.2f us (%.2f%%)", fileBrowserTime / 1000.0, (fileBrowserTime * 100.0) / totalFrameTime);
            ImGui::Text("  ImGui::Render:  %6.2f us (%.2f%%)", imguiRenderTime / 1000.0, (imguiRenderTime * 100.0) / totalFrameTime);
            ImGui::Text("  Clear:           %6.2f us (%.2f%%)", clearTime / 1000.0, (clearTime * 100.0) / totalFrameTime);
            ImGui::Text("  Background:      %6.2f us (%.2f%%)", backgroundTime / 1000.0, (backgroundTime * 100.0) / totalFrameTime);
            ImGui::Text("  RenderDrawData:  %6.2f us (%.2f%%)", renderDrawDataTime / 1000.0, (renderDrawDataTime * 100.0) / totalFrameTime);
            ImGui::Text("  Present:         %6.2f us (%.2f%%)", presentTime / 1000.0, (presentTime * 100.0) / totalFrameTime);
            ImGui::Text("  TOTAL FRAME:     %6.2f us (%.2f ms, %.1f FPS)", 
                       totalFrameTime / 1000.0, totalFrameTime / 1000.0, 1000000.0 / totalFrameTime);
        }
        ImGui::Separator();
        
        // Oscilloscope timings
        ImGui::Text("Oscilloscope Timings:");
        ImGui::Text("  Total:     %.2f ms", m_oscilloscopeTime);
        ImGui::Text("  Plot 0:    %.2f ms", m_oscilloscopePlot0Time);
        ImGui::Text("  Plot 1:    %.2f ms", m_oscilloscopePlot1Time);
        ImGui::Text("  Plot 2:    %.2f ms", m_oscilloscopePlot2Time);
    }
    ImGui::End();
}

