#include "UIManager.h"
#include "FilterWidget.h"
#include "IconsFontAwesome6.h"
#include "Logger.h"
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

UIManager::UIManager(SidPlayer& player, PlaylistManager& playlist, BackgroundManager& background, FileBrowser& fileBrowser, DatabaseManager& database, HistoryManager& history)
    : m_player(player), m_playlist(playlist), m_background(background), m_fileBrowser(fileBrowser), m_database(database), m_history(history),
      m_window(nullptr), m_renderer(nullptr), m_showFileDialog(false), m_isConfigTabActive(false), m_indexRequested(false),
      m_selectedSearchResult(-1), m_searchListFocused(false),
      m_databaseOperationInProgress(false), m_databaseOperationProgress(0.0f),
      m_filtersNeedUpdate(true), m_authorFilterWidget("Author", 200.0f), m_yearFilterWidget("Year", 150.0f),
      m_firstFilteredMatch(nullptr), m_filtersActive(false), m_shouldScrollToFirstMatch(false),
      m_cachedCurrentIndex(-1), m_navigationCacheValid(false) {
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
        LOG_INFO("FontAwesome chargé depuis: {}", fontPath.string());
    } else {
        LOG_WARNING("FontAwesome non trouvé, les icônes ne seront pas disponibles");
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
    
    // Rendu ImGui
    auto t8 = std::chrono::high_resolution_clock::now();
    ImGui::Render();
    auto t9 = std::chrono::high_resolution_clock::now();
    auto imguiRenderTime = std::chrono::duration_cast<std::chrono::microseconds>(t9 - t8).count();
    
    ImGuiIO& io = ImGui::GetIO();
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
    
    auto frameEnd = std::chrono::high_resolution_clock::now();
    auto totalFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count();
    
    // Afficher les timings toutes les 60 frames (environ 1 seconde à 60 FPS)
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        LOG_DEBUG("=== RENDER TIMINGS (frame {}) ===", frameCount);
        LOG_DEBUG("NewFrame:        {:6.2f} us ({:.2f}%)", newFrameTime / 1000.0, (newFrameTime * 100.0) / totalFrameTime);
        LOG_DEBUG("MainPanel:       {:6.2f} us ({:.2f}%)", mainPanelTime / 1000.0, (mainPanelTime * 100.0) / totalFrameTime);
        LOG_DEBUG("PlaylistPanel:   {:6.2f} us ({:.2f}%)", playlistPanelTime / 1000.0, (playlistPanelTime * 100.0) / totalFrameTime);
        LOG_DEBUG("FileBrowser:     {:6.2f} us ({:.2f}%)", fileBrowserTime / 1000.0, (fileBrowserTime * 100.0) / totalFrameTime);
        LOG_DEBUG("ImGui::Render:   {:6.2f} us ({:.2f}%)", imguiRenderTime / 1000.0, (imguiRenderTime * 100.0) / totalFrameTime);
        LOG_DEBUG("Clear:           {:6.2f} us ({:.2f}%)", clearTime / 1000.0, (clearTime * 100.0) / totalFrameTime);
        LOG_DEBUG("Background:      {:6.2f} us ({:.2f}%)", backgroundTime / 1000.0, (backgroundTime * 100.0) / totalFrameTime);
        LOG_DEBUG("RenderDrawData:  {:6.2f} us ({:.2f}%)", renderDrawDataTime / 1000.0, (renderDrawDataTime * 100.0) / totalFrameTime);
        LOG_DEBUG("Present:         {:6.2f} us ({:.2f}%)", presentTime / 1000.0, (presentTime * 100.0) / totalFrameTime);
        LOG_DEBUG("TOTAL FRAME:     {:6.2f} us ({:.2f} ms, {:.1f} FPS)", totalFrameTime / 1000.0, totalFrameTime / 1000.0, 1000000.0 / totalFrameTime);
        LOG_DEBUG("===================================");
    }
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
    auto t0 = std::chrono::high_resolution_clock::now();
    renderOscilloscopes();
    auto t1 = std::chrono::high_resolution_clock::now();
    static long long oscilloscopesTime = 0;
    oscilloscopesTime += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    
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

    auto t2 = std::chrono::high_resolution_clock::now();
    renderPlayerControls();
    auto t3 = std::chrono::high_resolution_clock::now();
    static long long playerControlsTime = 0;
    playerControlsTime += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    
    // Afficher les timings accumulés toutes les 60 frames
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        LOG_DEBUG("[PlayerTab] Oscilloscopes: {:.2f} us/frame avg, PlayerControls: {:.2f} us/frame avg",
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
    
    // Afficher les timings accumulés toutes les 60 frames
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        LOG_DEBUG("[Oscilloscopes] Total: {:.2f} us/frame avg, Plot0: {:.2f} us, Plot1: {:.2f} us, Plot2: {:.2f} us",
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
        
        // Widget de notation par étoiles
        const SidMetadata* metadata = m_database.getMetadata(m_player.getCurrentFile());
        if (metadata) {
            int currentRating = m_history.getRating(metadata->metadataHash);
            int prevRating = currentRating;
            
            ImGui::Text("Rating:");
            if (renderStarRating("##trackRating", &currentRating, 5)) {
                // Le rating a changé, sauvegarder
                if (currentRating != prevRating) {
                    m_history.updateRating(metadata->metadataHash, currentRating);
                    LOG_INFO("Rating mis à jour: {} étoiles pour {}", currentRating, metadata->title);
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        } else {
            // Debug : vérifier pourquoi les métadonnées ne sont pas disponibles
            LOG_DEBUG("Métadonnées non disponibles pour le fichier: {}", m_player.getCurrentFile());
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
    
    // Utiliser l'arbre filtré si des filtres sont actifs, sinon l'arbre original
    PlaylistNode* root = (m_filtersActive && m_filteredTreeRoot) ? m_filteredTreeRoot.get() : m_playlist.getRoot();
    PlaylistNode* currentNode = m_playlist.getCurrentNode();
    bool shouldScroll = m_playlist.shouldScrollToCurrent();
    
    size_t nodesRendered = 0;
    // Note: On ne compte plus totalNodes à chaque frame car c'est inutile
    // Le vrai problème n'est pas là - renderNode ne parcourt que les nœuds visibles
    
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
                    recordHistoryEntry(node->filepath);
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
    
    // Log pour mesurer le problème
    if (renderTime > 10) {
        LOG_WARNING("[UI] renderPlaylistTree: {} ms ({} nœuds rendus)", renderTime, nodesRendered);
    } else {
        LOG_DEBUG("[UI] renderPlaylistTree: {} ms ({} nœuds rendus)", renderTime, nodesRendered);
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
        // Reconstruire le cache
        m_cachedAllFiles.clear();
        
        if (m_filtersActive && m_filteredTreeRoot) {
            // Collecter tous les fichiers de l'arbre filtré
            std::function<void(PlaylistNode*)> collectFiles = [&](PlaylistNode* node) {
                if (!node) return;
                if (!node->isFolder && !node->filepath.empty()) {
                    m_cachedAllFiles.push_back(node);
                }
                for (auto& child : node->children) {
                    collectFiles(child.get());
                }
            };
            for (auto& child : m_filteredTreeRoot->children) {
                collectFiles(child.get());
            }
        } else {
            // Utiliser l'arbre original
            m_cachedAllFiles = m_playlist.getAllFiles();
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
                recordHistoryEntry(filepath);
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

void UIManager::recordHistoryEntry(const std::string& filepath) {
    if (filepath.empty()) return;
    
    // Récupérer les métadonnées du fichier
    const SidMetadata* metadata = m_database.getMetadata(filepath);
    if (!metadata) return; // Pas de métadonnées = pas d'historique
    
    // Enregistrer dans l'historique
    m_history.addEntry(metadata->title, metadata->author, metadata->metadataHash);
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
        LOG_INFO("[UI] rebuildFilepathToHashCache(): {} ms ({} fichiers mis en cache)", totalTime, cached);
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

void UIManager::invalidateNavigationCache() {
    m_navigationCacheValid = false;
    m_cachedAllFiles.clear();
    m_cachedCurrentIndex = -1;
}

void UIManager::rebuildFilteredTree() {
    // Invalider le cache de navigation car l'arbre filtré change
    invalidateNavigationCache();
    
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
        LOG_DEBUG("[Filter] Arbre filtré créé: {} fichiers (filtre auteur='{}', année='{}')", 
                  filesFiltered, m_filterAuthor, m_filterYear);
        
        // Afficher les 5 premiers fichiers pour debug
        int debugCount = 0;
        std::function<void(PlaylistNode*)> debugFiles = [&](PlaylistNode* n) {
            if (!n || debugCount >= 5) return;
            if (!n->isFolder && !n->filepath.empty()) {
                const SidMetadata* meta = m_database.getMetadata(n->filepath);
                if (meta) {
                    LOG_DEBUG("  [{}] {} -> author='{}'", debugCount, n->name, meta->author);
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
        invalidateNavigationCache();
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
    
    // Afficher "Drag & drop" dans le tree view si vide
    PlaylistNode* root = (m_filtersActive && m_filteredTreeRoot) ? m_filteredTreeRoot.get() : m_playlist.getRoot();
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
    
    // Afficher les timings accumulés toutes les 60 frames
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        LOG_DEBUG("[ExplorerTab] Total: {:.2f} us/frame avg, Tree: {:.2f} us/frame avg, Nav: {:.2f} us/frame avg",
                  explorerTabTime / 60.0 / 1000.0, treeTime / 60.0 / 1000.0, navTime / 60.0 / 1000.0);
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
                
                std::sort(sortedEntries.begin(), sortedEntries.end(), [&sortSpecs](const HistoryEntry& a, const HistoryEntry& b) {
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
                                delta = (int)a.playCount - (int)b.playCount;
                                break;
                            case 3: // Rating
                                delta = a.rating - b.rating;
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
            if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                // Trouver le fichier correspondant dans la playlist par metadataHash
                auto allFiles = m_playlist.getAllFiles();
                for (PlaylistNode* node : allFiles) {
                    if (!node || node->filepath.empty()) continue;
                    
                    const SidMetadata* metadata = m_database.getMetadata(node->filepath);
                    if (metadata && metadata->metadataHash == entry.metadataHash) {
                        // Sélectionner ce nœud
                        m_playlist.setCurrentNode(node);
                        m_playlist.setScrollToCurrent(true);
                        
                        // Charger et jouer le fichier
                        if (m_player.loadFile(node->filepath)) {
                            m_player.play();
                            recordHistoryEntry(node->filepath);
                        }
                        break;
                    }
                }
            }
            
            // Colonne Auteur
            ImGui::TableSetColumnIndex(1);
            std::string author = entry.author.empty() ? "(Inconnu)" : entry.author;
            ImGui::Text("%s", author.c_str());
            
            // Colonne Nombre de plays
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", entry.playCount);
            
            // Colonne Rating
            ImGui::TableSetColumnIndex(3);
            int rating = entry.rating;
            // Réduire la taille des étoiles dans l'historique en utilisant PushFont avec taille réduite
            ImFont* currentFont = ImGui::GetFont();
            float originalFontSize = ImGui::GetFontSize();
            float reducedFontSize = originalFontSize * 0.7f; // Réduire à 70%
            ImGui::PushFont(currentFont, reducedFontSize);
            // Afficher les étoiles
            for (int i = 1; i <= 5; i++) {
                if (i <= rating) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
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

    for (int i = 1; i <= max_stars; i++) {
        ImGui::PushID(i); // ID unique pour chaque étoile
        
        // Appliquer une couleur aux étoiles
        if (i <= *rating) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f)); // Orange vif pour les étoiles pleines
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
            *rating = i;
            changed = true;
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

