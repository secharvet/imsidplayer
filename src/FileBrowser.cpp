#include "FileBrowser.h"
#include "IconsFontAwesome6.h"
#include "imgui.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#ifdef _WIN32
#include <cstdlib>
#else
#include <cstdlib>
#endif

FileBrowser::FileBrowser() : m_currentDirectory(std::filesystem::current_path()), m_filterExtension(".sid") {
    updateDirectoryList();
}

void FileBrowser::render(bool& showDialog, std::string& selectedFilePath) {
    if (!showDialog) return;
    
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Select a SID file", &showDialog, ImGuiWindowFlags_None);
    
    // Navigation bar
    ImGui::Text("Directory:");
    ImGui::SameLine();
    char dirBuffer[512];
    strncpy(dirBuffer, m_currentDirectory.string().c_str(), sizeof(dirBuffer) - 1);
    dirBuffer[sizeof(dirBuffer) - 1] = '\0';
    
    if (ImGui::InputText("##dir", dirBuffer, sizeof(dirBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::filesystem::path newPath(dirBuffer);
        if (std::filesystem::exists(newPath) && std::filesystem::is_directory(newPath)) {
            m_currentDirectory = newPath;
            updateDirectoryList();
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Actualiser")) {
        updateDirectoryList();
    }
    
    ImGui::Separator();
    
    // Navigation buttons
    if (ImGui::Button(ICON_FA_CHEVRON_LEFT " Previous")) {
        goToParent();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_HOUSE " Accueil")) {
        goToHome();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER " Racine")) {
        goToRoot();
    }
    
    ImGui::Separator();
    
    // File list
    ImGui::BeginChild("FileList", ImVec2(0, -80), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    for (size_t i = 0; i < m_directoryEntries.size(); ++i) {
        const std::string& entry = m_directoryEntries[i];
        bool isDirectory = entry.find("[DIR]") == 0;
        std::string displayName = isDirectory ? entry.substr(6) : entry;
        
        if (isDirectory) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[DIR] %s", displayName.c_str());
        } else {
            ImGui::Text("      %s", displayName.c_str());
        }
        
        if (ImGui::IsItemClicked()) {
            if (isDirectory) {
                m_currentDirectory /= displayName;
                updateDirectoryList();
            } else {
                selectedFilePath = (m_currentDirectory / displayName).string();
            }
        }
        
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (isDirectory) {
                m_currentDirectory /= displayName;
                updateDirectoryList();
            } else {
                selectedFilePath = (m_currentDirectory / displayName).string();
            }
        }
    }
    
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // Selected file
    ImGui::Text("Fichier sélectionné:");
    ImGui::SameLine();
    if (selectedFilePath.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Aucun");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", 
            std::filesystem::path(selectedFilePath).filename().string().c_str());
    }
    
    ImGui::Spacing();
    
    // Action buttons
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load", ImVec2(150, 0))) {
        // Le chargement sera géré par l'appelant
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(150, 0))) {
        showDialog = false;
        selectedFilePath = "";
    }
    
    ImGui::End();
}

void FileBrowser::updateDirectoryList() {
    refreshDirectoryList();
}

void FileBrowser::goToParent() {
    if (m_currentDirectory.has_parent_path() && m_currentDirectory != m_currentDirectory.root_path()) {
        m_currentDirectory = m_currentDirectory.parent_path();
        updateDirectoryList();
    }
}

void FileBrowser::goToHome() {
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
    if (!home) home = getenv("APPDATA");
    m_currentDirectory = std::filesystem::path(home ? home : "C:\\");
#else
    const char* home = getenv("HOME");
    m_currentDirectory = std::filesystem::path(home ? home : "/");
#endif
    updateDirectoryList();
}

void FileBrowser::goToRoot() {
    m_currentDirectory = m_currentDirectory.root_path();
    updateDirectoryList();
}

void FileBrowser::refreshDirectoryList() {
    m_directoryEntries.clear();
    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_currentDirectory)) {
            if (entry.is_directory()) {
                m_directoryEntries.push_back("[DIR] " + entry.path().filename().string());
            } else {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == m_filterExtension || m_filterExtension.empty()) {
                    m_directoryEntries.push_back(entry.path().filename().string());
                }
            }
        }
        std::sort(m_directoryEntries.begin(), m_directoryEntries.end());
    } catch (const std::exception& e) {
        std::cerr << "Erreur lecture répertoire: " << e.what() << std::endl;
    }
}

