#include "FilterWidget.h"
#include "IconsFontAwesome6.h"
#include <algorithm>
#include <cctype>

FilterWidget::FilterWidget(const std::string& label, float inputWidth)
    : m_label(label), m_inputWidth(inputWidth), m_selectedIndex(-1), m_listFocused(false) {
}

bool FilterWidget::render(const std::string& currentValue, 
                          const std::vector<std::string>& availableItems,
                          std::function<void(const std::string&)> onValueChanged,
                          bool downArrowPressed) {
    bool valueChanged = false;
    
    // Label
    ImGui::Text("%s:", m_label.c_str());
    ImGui::SameLine();
    
    // Champ de saisie
    ImGui::SetNextItemWidth(m_inputWidth);
    
    char buffer[256];
    strncpy(buffer, m_query.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    bool textChanged = ImGui::InputTextWithHint(("##" + m_label + "Input").c_str(), 
                                                 currentValue.empty() ? "All" : currentValue.c_str(), 
                                                 buffer, sizeof(buffer));
    
    // Gérer la navigation clavier : flèche bas depuis le champ de recherche
    bool downArrowFromSearch = ImGui::IsItemActive() && downArrowPressed;
    if (downArrowFromSearch && !m_listFocused) {
        if (!m_filteredItems.empty()) {
            m_listFocused = true;
            m_selectedIndex = 0;
        }
    }
    
    if (textChanged) {
        m_query = buffer;
        m_selectedIndex = -1;
        m_listFocused = false;
        updateFilteredItems(availableItems);
    }
    
    // Bouton pour supprimer le filtre
    if (!currentValue.empty()) {
        ImGui::SameLine();
        std::string clearButtonId = std::string(ICON_FA_XMARK) + "##clear" + m_label;
        if (ImGui::Button(clearButtonId.c_str())) {
            onValueChanged("");
            m_query.clear();
            m_selectedIndex = -1;
            m_listFocused = false;
            updateFilteredItems(availableItems);
            valueChanged = true;
        }
    }
    
    // Afficher la liste déroulante si on tape dans le champ
    if (!m_query.empty() && !m_filteredItems.empty()) {
        ImGui::Spacing();
        // Utiliser toute la largeur disponible (comme la recherche fuzzy)
        float availableWidth = ImGui::GetContentRegionAvail().x;
        // Calculer la hauteur en fonction du nombre d'items, sans limite maximale
        // (25 pixels par item, avec un minimum de 100px et un maximum raisonnable pour éviter les listes trop grandes)
        float calculatedHeight = m_filteredItems.size() * 25.0f;
        float maxHeight = 600.0f; // Hauteur maximale raisonnable pour éviter les listes trop grandes
        float listHeight = std::min(calculatedHeight, maxHeight);
        ImGui::BeginChild(("##" + m_label + "FilterList").c_str(), 
                         ImVec2(availableWidth, listHeight), 
                         true);
        
        // Gérer la navigation clavier dans la liste
        if (m_listFocused && !downArrowFromSearch) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                if (m_selectedIndex > 0) {
                    m_selectedIndex--;
                } else {
                    // Retour au champ de recherche
                    m_listFocused = false;
                    m_selectedIndex = -1;
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                if (m_selectedIndex < (int)m_filteredItems.size() - 1) {
                    m_selectedIndex++;
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_filteredItems.size()) {
                    onValueChanged(m_filteredItems[m_selectedIndex]);
                    m_query.clear();
                    m_selectedIndex = -1;
                    m_listFocused = false;
                    valueChanged = true;
                }
            }
        }
        
        // Option "All" (pas de filtre)
        bool isAllSelected = currentValue.empty();
        if (ImGui::Selectable("All", isAllSelected)) {
            onValueChanged("");
            m_query.clear();
            m_selectedIndex = -1;
            m_listFocused = false;
            valueChanged = true;
        }
        
        // Afficher les items filtrés
        for (size_t i = 0; i < m_filteredItems.size(); ++i) {
            bool isSelected = (m_selectedIndex == (int)i && m_listFocused) || (currentValue == m_filteredItems[i]);
            
            if (isSelected && m_listFocused) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.4f, 0.6f, 0.5f));
            }
            
            if (ImGui::Selectable(m_filteredItems[i].c_str(), isSelected)) {
                onValueChanged(m_filteredItems[i]);
                m_query.clear();
                m_selectedIndex = -1;
                m_listFocused = false;
                valueChanged = true;
            }
            
            if (isSelected && m_listFocused) {
                ImGui::PopStyleColor(2);
                ImGui::SetScrollHereY(0.5f);
            }
        }
        
        ImGui::EndChild();
    } else {
        m_listFocused = false;
    }
    
    return valueChanged;
}

void FilterWidget::updateFilteredItems(const std::vector<std::string>& availableItems) {
    m_filteredItems.clear();
    
    if (m_query.empty()) {
        // Si pas de requête, afficher tous les items
        m_filteredItems = availableItems;
        return;
    }
    
    // Convertir la requête en minuscules
    std::string queryLower = m_query;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
    
    // Filtrer les items
    for (const std::string& item : availableItems) {
        std::string itemLower = item;
        std::transform(itemLower.begin(), itemLower.end(), itemLower.begin(), ::tolower);
        if (itemLower.find(queryLower) != std::string::npos) {
            m_filteredItems.push_back(item);
        }
    }
}

void FilterWidget::reset() {
    m_query.clear();
    m_selectedIndex = -1;
    m_listFocused = false;
    m_filteredItems.clear();
}

void FilterWidget::initialize(const std::vector<std::string>& availableItems) {
    updateFilteredItems(availableItems);
}

