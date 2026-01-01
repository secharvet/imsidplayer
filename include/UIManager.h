#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "SidPlayer.h"
#include "PlaylistManager.h"
#include "BackgroundManager.h"
#include "FileBrowser.h"
#include "DatabaseManager.h"
#include "FilterWidget.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

class UIManager {
public:
    UIManager(SidPlayer& player, PlaylistManager& playlist, BackgroundManager& background, FileBrowser& fileBrowser, DatabaseManager& database);
    
    // Initialiser ImGui (polices, styles)
    bool initialize(SDL_Window* window, SDL_Renderer* renderer);
    
    // Rendu d'une frame
    void render();
    
    // Gestion des événements
    bool handleEvent(const SDL_Event& event);
    
    // Nettoyage
    void shutdown();
    
    // Getters/Setters
    bool isConfigTabActive() const { return m_isConfigTabActive; }
    bool& showFileDialog() { return m_showFileDialog; }
    std::string& selectedFilePath() { return m_selectedFilePath; }
    bool& indexRequested() { return m_indexRequested; }
    
    // Reconstruire le cache filepath -> metadataHash (appelé après indexation)
    void rebuildFilepathToHashCache();
    
    // Marquer que les filtres doivent être mis à jour (quand la playlist change)
    void markFiltersNeedUpdate() { m_filtersNeedUpdate = true; }
    
    // Vérifier si une opération de base de données est en cours
    // Ces méthodes sont appelées depuis Application pour mettre à jour l'état
    void setDatabaseOperationInProgress(bool inProgress, const std::string& status = "", float progress = 0.0f);
    bool isDatabaseOperationInProgress() const { return m_databaseOperationInProgress; }
    std::string getDatabaseOperationStatus() const { return m_databaseOperationStatus; }
    float getDatabaseOperationProgress() const { return m_databaseOperationProgress; }
    
private:
    SidPlayer& m_player;
    PlaylistManager& m_playlist;
    BackgroundManager& m_background;
    FileBrowser& m_fileBrowser;
    DatabaseManager& m_database;
    
    // État des opérations de base de données (mis à jour depuis Application)
    bool m_databaseOperationInProgress;
    std::string m_databaseOperationStatus;
    float m_databaseOperationProgress;
    
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    
    // Variables UI
    bool m_showFileDialog;
    std::string m_selectedFilePath;
    bool m_isConfigTabActive;
    bool m_indexRequested;
    std::string m_searchQuery;  // Requête de recherche fuzzy
    std::vector<const SidMetadata*> m_searchResults;  // Résultats de recherche (max 10)
    int m_selectedSearchResult;  // Index du résultat sélectionné dans la liste (-1 = aucun, focus sur champ)
    bool m_searchListFocused;  // True si la liste de résultats a le focus (navigation clavier)
    std::unordered_map<std::string, uint32_t> m_filepathToHashCache;  // Cache filepath -> metadataHash pour éviter les lookups répétés
    
    // Filtres multicritères
    std::string m_filterAuthor;  // Filtre par auteur (vide = pas de filtre)
    std::string m_filterYear;   // Filtre par année (vide = pas de filtre)
    std::vector<std::string> m_availableAuthors;  // Liste des auteurs disponibles
    std::vector<std::string> m_availableYears;    // Liste des années disponibles
    bool m_filtersNeedUpdate;  // Flag pour mettre à jour les listes de filtres
    FilterWidget m_authorFilterWidget;  // Widget de filtre pour les auteurs
    FilterWidget m_yearFilterWidget;    // Widget de filtre pour les années
    std::unique_ptr<PlaylistNode> m_filteredTreeRoot;  // Copie filtrée de l'arbre (élaguée)
    PlaylistNode* m_firstFilteredMatch;  // Premier nœud qui correspond aux filtres (pour scroll)
    bool m_filtersActive;  // True si au moins un filtre est actif (item sélectionné dans la liste)
    bool m_shouldScrollToFirstMatch;  // Flag pour scroller vers le premier match après reconstruction de l'arbre
    
    // Méthodes de rendu
    void renderMainPanel();
    void renderPlayerTab();
    void renderConfigTab();
    void renderPlaylistPanel();
    void renderFileBrowser();
    
    // Composants UI
    void renderOscilloscopes();
    void renderPlayerControls();
    void renderPlaylistTree();
    void renderPlaylistNavigation();
    void renderFilters();  // Afficher les filtres multicritères
    
    // Helpers
    void renderBackground();
    void updateSearchResults();  // Mettre à jour les résultats de recherche fuzzy (max 10)
    void navigateToFile(const std::string& filepath);  // Naviguer vers un fichier dans l'arbre
    void updateFilterLists();  // Mettre à jour les listes d'auteurs et d'années disponibles
    bool matchesFilters(PlaylistNode* node) const;  // Vérifier si un nœud correspond aux filtres
    void rebuildFilteredTree();  // Reconstruire l'arbre filtré (élaguer les nœuds vides)
};

#endif // UI_MANAGER_H

