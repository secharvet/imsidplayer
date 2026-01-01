#include "UIManager.h"
#include "FilterWidget.h"
#include "IconsFontAwesome6.h"
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

namespace fs = std::filesystem;

UIManager::UIManager(SidPlayer& player, PlaylistManager& playlist, BackgroundManager& background, FileBrowser& fileBrowser, DatabaseManager& database)
    : m_player(player), m_playlist(playlist), m_background(background), m_fileBrowser(fileBrowser), m_database(database),
      m_window(nullptr), m_renderer(nullptr), m_showFileDialog(false), m_isConfigTabActive(false), m_indexRequested(false),
      m_selectedSearchResult(-1), m_searchListFocused(false),
      m_databaseOperationInProgress(false), m_databaseOperationProgress(0.0f),
      m_filtersNeedUpdate(true), m_authorFilterWidget("Author", 200.0f), m_yearFilterWidget("Year", 150.0f),
      m_firstFilteredMatch(nullptr), m_filtersActive(false), m_shouldScrollToFirstMatch(false) {
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
        fs::current_path() / "fa-solid-900.ttf",
        fs::current_path().parent_path() / "fa-solid-900.ttf"
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
        std::cout << "FontAwesome chargé depuis: " << fontPath << std::endl;
    } else {
        std::cerr << "Attention: FontAwesome non trouvé, les icônes ne seront pas disponibles" << std::endl;
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
    // Réinitialiser isConfigTabActive au début de chaque frame
    m_isConfigTabActive = false;
    
    // Démarrer le frame ImGui
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    
    renderMainPanel();
    renderPlaylistPanel();
    renderFileBrowser();
    
    // Rendu
    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();
    SDL_RenderSetScale(m_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    
    // Effacer le renderer
    ImVec4 clearColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    SDL_SetRenderDrawColor(m_renderer, 
        (Uint8)(clearColor.x * 255), 
        (Uint8)(clearColor.y * 255), 
        (Uint8)(clearColor.z * 255), 
        (Uint8)(clearColor.w * 255));
    SDL_RenderClear(m_renderer);
    
    // Afficher l'image de fond
    renderBackground();
    
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);
}

bool UIManager::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    return event.type == SDL_QUIT;
}

void UIManager::shutdown() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
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
    renderOscilloscopes();
    
    // Section informations
    ImGui::Text("Current file:");
    if (!m_player.getCurrentFile().empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", 
            fs::path(m_player.getCurrentFile()).filename().string().c_str());
        ImGui::Separator();
        ImGui::TextWrapped("Information: %s", m_player.getTuneInfo().c_str());
        ImGui::Text("SID Model: %s", m_player.getSidModel().c_str());
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No file loaded");
    }
    
    ImGui::Separator();
    ImGui::Spacing();

    renderPlayerControls();
}

void UIManager::renderOscilloscopes() {
    if (m_player.getCurrentFile().empty()) return;
    
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
    ImGui::PlotLines("##plot", voice0, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                    ImVec2(plotWidth, plotHeight));
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
    ImGui::PlotLines("##plot", voice1, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                    ImVec2(plotWidth, plotHeight));
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
    ImGui::PlotLines("##plot", voice2, SidPlayer::OSCILLOSCOPE_SIZE, 0, nullptr, -1.0f, 1.0f, 
                    ImVec2(plotWidth, plotHeight));
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
}

void UIManager::renderPlayerControls() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 12.0f));
    
    if (m_player.getCurrentFile().empty()) {
        ImGui::BeginDisabled();
    }

    float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    
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

    if (m_player.getCurrentFile().empty()) {
        ImGui::EndDisabled();
    }
    
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::Separator();

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
        if (ImGui::SliderInt("##alpha", &alpha, 0, 255, "%d")) {
            m_background.setAlpha(alpha);
        }
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
    ImGui::Begin("Playlist", nullptr, 
        ImGuiWindowFlags_NoTitleBar | 
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoCollapse);
    
    ImGui::Text("Drag & drop .sid files or folders here");
    ImGui::Separator();
    
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
    
    bool textChanged = ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer), 
                                        dbOperationInProgress ? ImGuiInputTextFlags_ReadOnly : 0);
    
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
        m_searchQuery = searchBuffer;
        m_selectedSearchResult = -1;
        m_searchListFocused = false; // Reset focus sur liste quand on tape
        // Mettre à jour les résultats de recherche
        updateSearchResults();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "##clearsearch")) {
        m_searchQuery.clear();
        m_searchResults.clear();
        m_selectedSearchResult = -1;
        m_searchListFocused = false;
    }
    
    // Afficher les résultats dans une zone dédiée sous le champ de recherche
    if (!m_searchQuery.empty() && !m_searchResults.empty()) {
        ImGui::Spacing();
        ImGui::BeginChild("##searchResultsList", ImVec2(0, std::min(200.0f, m_searchResults.size() * 25.0f)), true);
        
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
    
    renderPlaylistTree();
    renderPlaylistNavigation();
    
    ImGui::End();
}

void UIManager::renderPlaylistTree() {
    auto renderStart = std::chrono::high_resolution_clock::now();
    
    ImGui::BeginChild("PlaylistTree", ImVec2(0, -60), true);
    
    // Utiliser l'arbre filtré si des filtres sont actifs, sinon l'arbre original
    PlaylistNode* root = (m_filtersActive && m_filteredTreeRoot) ? m_filteredTreeRoot.get() : m_playlist.getRoot();
    PlaylistNode* currentNode = m_playlist.getCurrentNode();
    bool shouldScroll = m_playlist.shouldScrollToCurrent();
    
    size_t nodesRendered = 0;
    
    std::function<bool(PlaylistNode*)> isParentOfCurrent = [&](PlaylistNode* node) -> bool {
        if (!node || !currentNode) return false;
        
        // Vérifier que currentNode est valide (pas dans l'arbre filtré supprimé)
        // En remontant depuis currentNode, on doit pouvoir atteindre la racine de l'arbre actuel
        PlaylistNode* parent = currentNode->parent;
        if (!parent) return false; // currentNode n'a pas de parent, donc node ne peut pas être son parent
        
        while (parent) {
            if (parent == node) return true;
            // Vérifier que parent->parent existe avant d'y accéder
            if (!parent->parent) break;
            parent = parent->parent;
        }
        return false;
    };
    
    std::function<void(PlaylistNode*, int)> renderNode = [&](PlaylistNode* node, int depth) {
        if (!node) return;
        
        nodesRendered++;
        ImGui::PushID(node);
        
        bool isCurrent = (currentNode == node && !node->filepath.empty());
        bool isSelected = (currentNode == node);
        bool shouldOpen = isParentOfCurrent(node) || (m_filtersActive && node->isFolder); // Toujours ouvrir si filtres actifs
        
        // Vérifier si c'est le premier match à scroller
        bool isFirstMatch = (m_filtersActive && m_firstFilteredMatch == node);
        
        if (node->isFolder) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
            if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
            if (shouldOpen || m_filtersActive) flags |= ImGuiTreeNodeFlags_DefaultOpen; // Toujours ouvrir si filtres actifs
            
            bool nodeOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);
            
            if (nodeOpen) {
                for (auto& child : node->children) {
                    renderNode(child.get(), depth + 1);
                }
                ImGui::TreePop();
            }
        } else {
            float indentAmount = depth * 5.0f;
            if (depth > 0) {
                ImGui::Indent(indentAmount);
            }
            
            // Mettre en évidence le fichier courant ou le premier match
            if (isCurrent || isFirstMatch) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f)); // Bleu pour le fichier courant ou premier match
            }
            
            if (ImGui::Selectable(node->name.c_str(), isSelected)) {
                m_playlist.setCurrentNode(node);
                if (!node->filepath.empty() && m_player.loadFile(node->filepath)) {
                    m_player.play();
                }
            }
            
            // Scroller vers le fichier courant ou le premier match
            if ((isCurrent && shouldScroll) || (isFirstMatch && m_shouldScrollToFirstMatch)) {
                ImGui::SetScrollHereY(0.5f);
                if (isFirstMatch) {
                    m_shouldScrollToFirstMatch = false;  // Ne scroller qu'une fois
                }
            }
            
            if (isCurrent || isFirstMatch) {
                ImGui::PopStyleColor();
            }
            
            if (depth > 0) {
                ImGui::Unindent(indentAmount);
            }
        }
        
        ImGui::PopID();
    };
    
    for (auto& child : root->children) {
        renderNode(child.get(), 0);
    }
    
    if (shouldScroll) {
        m_playlist.setScrollToCurrent(false);
    }
    
    auto renderEnd = std::chrono::high_resolution_clock::now();
    auto renderTime = std::chrono::duration_cast<std::chrono::milliseconds>(renderEnd - renderStart).count();
    // Log seulement si problème de performance (> 10ms)
    if (renderTime > 10) {
        printf("[UI] renderPlaylistTree: %ld ms (%zu nœuds rendus)\n", renderTime, nodesRendered);
    }
    
    ImGui::EndChild();
}

void UIManager::renderPlaylistNavigation() {
    ImGui::Spacing();
    
    auto allFiles = m_playlist.getAllFiles();
    PlaylistNode* currentNode = m_playlist.getCurrentNode();
    
    float navButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    
    if (ImGui::Button(ICON_FA_BACKWARD_STEP "", ImVec2(navButtonWidth, 0))) {
        PlaylistNode* prev = m_playlist.getPreviousFile();
        if (prev) {
            m_playlist.setCurrentNode(prev);
            if (m_player.loadFile(prev->filepath)) {
                m_player.play();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP "", ImVec2(navButtonWidth, 0))) {
        PlaylistNode* next = m_playlist.getNextFile();
        if (next) {
            m_playlist.setCurrentNode(next);
            if (m_player.loadFile(next->filepath)) {
                m_player.play();
            }
        }
    }
}

void UIManager::renderFileBrowser() {
    if (!m_showFileDialog) return;
    
    m_fileBrowser.render(m_showFileDialog, m_selectedFilePath);
    
    if (!m_selectedFilePath.empty() && fs::exists(m_selectedFilePath)) {
        if (m_player.loadFile(m_selectedFilePath)) {
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
}

void UIManager::navigateToFile(const std::string& filepath) {
    if (filepath.empty()) return;
    
    // Trouver le nœud correspondant dans la playlist
    auto allFiles = m_playlist.getAllFiles();
    for (PlaylistNode* node : allFiles) {
        if (node && node->filepath == filepath) {
            // Sélectionner ce nœud
            m_playlist.setCurrentNode(node);
            m_playlist.setScrollToCurrent(true);
            
            // Charger et jouer le fichier
            if (m_player.loadFile(filepath)) {
                m_player.play();
            }
            
            // Effacer la recherche
            m_searchQuery.clear();
            m_searchResults.clear();
            m_selectedSearchResult = -1;
            m_searchListFocused = false;
            
            // Note : on ne restaure pas les filtres ici car l'utilisateur a peut-être voulu les garder
            
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
        printf("[UI] rebuildFilepathToHashCache(): %ld ms (%zu fichiers mis en cache)\n",
               totalTime, cached);
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
    
    // Reconstruire l'arbre filtré si des filtres sont actifs
    if (m_filtersActive) {
        rebuildFilteredTree();
    }
}

bool UIManager::matchesFilters(PlaylistNode* node) const {
    if (!node) return false;
    
    // Les dossiers passent toujours (on filtre seulement les fichiers)
    if (node->isFolder) {
        return true;
    }
    
    // Si aucun filtre n'est actif, tout passe
    if (m_filterAuthor.empty() && m_filterYear.empty()) {
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
    
    // Vérifier le filtre auteur (comparaison exacte, sensible à la casse)
    if (!m_filterAuthor.empty()) {
        // Comparaison exacte : l'auteur doit être exactement égal au filtre
        // IMPORTANT : on compare uniquement le champ author, pas le title ni le filename
        if (metadata->author != m_filterAuthor) {
            return false;
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
        
        // Comparer l'année extraite avec le filtre
        if (yearFromReleased != m_filterYear) {
            return false;
        }
    }
    
    return true;
}

void UIManager::rebuildFilteredTree() {
    // Si aucun filtre n'est actif, ne pas créer d'arbre filtré
    if (!m_filtersActive) {
        m_filteredTreeRoot.reset();
        m_firstFilteredMatch = nullptr;
        m_shouldScrollToFirstMatch = false;
        return;
    }
    
    // Créer la fonction de filtre qui vérifie UNIQUEMENT les fichiers (pas les dossiers)
    // IMPORTANT : on filtre uniquement sur le champ author des métadonnées, pas sur le title ni le filename
    auto filterFunc = [this](PlaylistNode* node) -> bool {
        // Les dossiers passent toujours (on filtre seulement les fichiers)
        if (node->isFolder) {
            return true;
        }
        
        // Pour les fichiers, utiliser matchesFilters qui vérifie les métadonnées
        return matchesFilters(node);
    };
    
    // Créer l'arbre filtré depuis la racine originale
    PlaylistNode* originalRoot = m_playlist.getRoot();
    if (!originalRoot) {
        m_filteredTreeRoot.reset();
        m_firstFilteredMatch = nullptr;
        m_shouldScrollToFirstMatch = false;
        return;
    }
    
    // Créer un nœud racine pour l'arbre filtré
    m_filteredTreeRoot = std::make_unique<PlaylistNode>("Playlist", "", true);
    
    // Filtrer chaque enfant de la racine
    size_t filesFiltered = 0;
    for (auto& child : originalRoot->children) {
        auto filteredChild = m_playlist.createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            filteredChild->parent = m_filteredTreeRoot.get();
            m_filteredTreeRoot->children.push_back(std::move(filteredChild));
        }
    }
    
    // Compter les fichiers dans l'arbre filtré pour debug
    std::function<void(PlaylistNode*)> countFiles = [&](PlaylistNode* n) {
        if (!n) return;
        if (!n->isFolder && !n->filepath.empty()) {
            filesFiltered++;
        }
        for (auto& c : n->children) {
            countFiles(c.get());
        }
    };
    for (auto& child : m_filteredTreeRoot->children) {
        countFiles(child.get());
    }
    
    // Trouver le premier match pour le scroll (doit être un fichier qui matche)
    m_firstFilteredMatch = m_playlist.findFirstMatchingNode(m_filteredTreeRoot.get(), filterFunc);
    m_shouldScrollToFirstMatch = (m_firstFilteredMatch != nullptr);
    
    // Debug : afficher le nombre de fichiers filtrés et quelques exemples
    if (m_filtersActive) {
        printf("[Filter] Arbre filtré créé: %zu fichiers (filtre auteur='%s', année='%s')\n", 
               filesFiltered, m_filterAuthor.c_str(), m_filterYear.c_str());
        
        // Afficher les 5 premiers fichiers pour debug
        int debugCount = 0;
        std::function<void(PlaylistNode*)> debugFiles = [&](PlaylistNode* n) {
            if (!n || debugCount >= 5) return;
            if (!n->isFolder && !n->filepath.empty()) {
                const SidMetadata* meta = m_database.getMetadata(n->filepath);
                if (meta) {
                    printf("  [%d] %s -> author='%s'\n", debugCount, n->name.c_str(), meta->author.c_str());
                    debugCount++;
                }
            }
            for (auto& c : n->children) {
                debugFiles(c.get());
            }
        };
        for (auto& child : m_filteredTreeRoot->children) {
            debugFiles(child.get());
        }
    }
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
    
    // Filtre Auteur
    // Mode 1 : Recherche (focus) - Mode 2 : Sélection (arbre filtré)
    bool authorChanged = m_authorFilterWidget.render(m_filterAuthor, m_availableAuthors, 
        [this](const std::string& value) {
            m_filterAuthor = value;
            // Mode 2 : Quand on sélectionne un item, activer le filtre et construire l'arbre filtré
            if (!value.empty()) {
                m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty();
                rebuildFilteredTree();
            } else {
                // Si on efface le filtre, désactiver et revenir à l'arbre original
                m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty();
                if (!m_filtersActive) {
                    // IMPORTANT : Réinitialiser currentNode car il pointe vers l'arbre filtré qui va être supprimé
                    PlaylistNode* oldCurrentNode = m_playlist.getCurrentNode();
                    if (oldCurrentNode && !oldCurrentNode->filepath.empty()) {
                        // Chercher le nœud correspondant dans l'arbre original par filepath
                        PlaylistNode* originalNode = m_playlist.findNodeByPath(oldCurrentNode->filepath);
                        if (originalNode) {
                            m_playlist.setCurrentNode(originalNode);
                        } else {
                            // Si on ne trouve pas, réinitialiser à nullptr
                            m_playlist.setCurrentNode(nullptr);
                        }
                    }
                    
                    m_filteredTreeRoot.reset();
                    m_firstFilteredMatch = nullptr;
                } else {
                    rebuildFilteredTree();
                }
            }
        },
        downArrowPressed);
    
    // Filtre Année
    ImGui::SameLine(0, 20);  // Espacement
    bool yearChanged = m_yearFilterWidget.render(m_filterYear, m_availableYears,
        [this](const std::string& value) {
            m_filterYear = value;
            // Mode 2 : Quand on sélectionne un item, activer le filtre et construire l'arbre filtré
            if (!value.empty()) {
                m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty();
                rebuildFilteredTree();
            } else {
                // Si on efface le filtre, désactiver et revenir à l'arbre original
                m_filtersActive = !m_filterAuthor.empty() || !m_filterYear.empty();
                if (!m_filtersActive) {
                    // IMPORTANT : Réinitialiser currentNode car il pointe vers l'arbre filtré qui va être supprimé
                    // Trouver le nœud correspondant dans l'arbre original
                    PlaylistNode* oldCurrentNode = m_playlist.getCurrentNode();
                    if (oldCurrentNode && !oldCurrentNode->filepath.empty()) {
                        // Chercher le nœud correspondant dans l'arbre original par filepath
                        PlaylistNode* originalNode = m_playlist.findNodeByPath(oldCurrentNode->filepath);
                        if (originalNode) {
                            m_playlist.setCurrentNode(originalNode);
                        } else {
                            // Si on ne trouve pas, réinitialiser à nullptr
                            m_playlist.setCurrentNode(nullptr);
                        }
                    }
                    
                    m_filteredTreeRoot.reset();
                    m_firstFilteredMatch = nullptr;
                } else {
                    rebuildFilteredTree();
                }
            }
        },
        downArrowPressed);
    
    // Note : m_filtersActive est maintenant géré dans les callbacks ci-dessus
    // On ne reconstruit l'arbre filtré que quand on sélectionne un item (pas quand on tape)
}

