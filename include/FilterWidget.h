#ifndef FILTER_WIDGET_H
#define FILTER_WIDGET_H

#include "imgui.h"
#include <string>
#include <vector>
#include <functional>

/**
 * Widget générique pour les filtres avec recherche et liste déroulante
 * Réutilisable pour auteur, année, ou tout autre type de filtre
 */
class FilterWidget {
public:
    FilterWidget(const std::string& label, float inputWidth = 200.0f);
    
    /**
     * Affiche le widget de filtre
     * @param currentValue La valeur actuellement sélectionnée (vide = "All")
     * @param availableItems Liste de tous les items disponibles
     * @param onValueChanged Callback appelé quand la valeur change (paramètre = nouvelle valeur, ou "" pour "All")
     * @param downArrowPressed Si true, la flèche bas a été pressée (pour éviter double traitement)
     * @return true si la valeur a changé
     */
    bool render(const std::string& currentValue, 
                const std::vector<std::string>& availableItems,
                std::function<void(const std::string&)> onValueChanged,
                bool downArrowPressed = false);
    
    /**
     * Met à jour la liste filtrée selon la requête
     */
    void updateFilteredItems(const std::vector<std::string>& availableItems);
    
    /**
     * Réinitialise le widget (requête, sélection, focus)
     */
    void reset();
    
    /**
     * Initialise la liste filtrée avec tous les items disponibles
     */
    void initialize(const std::vector<std::string>& availableItems);

private:
    std::string m_label;              // Label du filtre (ex: "Author", "Year")
    float m_inputWidth;               // Largeur du champ de saisie
    std::string m_query;              // Requête de recherche
    std::vector<std::string> m_filteredItems;  // Items filtrés selon la requête
    int m_selectedIndex;              // Index de l'item sélectionné dans la liste (-1 = aucun)
    bool m_listFocused;               // True si la liste a le focus clavier
};

#endif // FILTER_WIDGET_H

