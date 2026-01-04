#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "SidPlayer.h"
#include "PlaylistManager.h"
#include "BackgroundManager.h"
#include "FileBrowser.h"
#include "DatabaseManager.h"
#include "HistoryManager.h"
#include "FilterWidget.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

class UIManager {
public:
    UIManager(SidPlayer& player, PlaylistManager& playlist, BackgroundManager& background, FileBrowser& fileBrowser, DatabaseManager& database, HistoryManager& history);
    
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
    HistoryManager& m_history;
    
    // État des opérations de base de données (mis à jour depuis Application)
    bool m_databaseOperationInProgress;
    std::string m_databaseOperationStatus;
    float m_databaseOperationProgress;
    
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    ImFont* m_boldFont;  // Police bold pour les dossiers
    
    // Variables UI
    bool m_showFileDialog;
    std::string m_selectedFilePath;
    bool m_isConfigTabActive;
    bool m_indexRequested;
    std::string m_searchQuery;  // Requête de recherche fuzzy
    std::vector<const SidMetadata*> m_searchResults;  // Résultats de recherche (max 10)
    int m_selectedSearchResult;  // Index du résultat sélectionné dans la liste (-1 = aucun, focus sur champ)
    bool m_searchListFocused;  // True si la liste de résultats a le focus (navigation clavier)
    std::string m_pendingSearchQuery;  // Requête en attente (pour debounce)
    std::chrono::high_resolution_clock::time_point m_lastSearchInputTime;  // Temps de la dernière frappe
    bool m_searchPending;  // True si une recherche est en attente
    std::unordered_map<std::string, uint32_t> m_filepathToHashCache;  // Cache filepath -> metadataHash pour éviter les lookups répétés
    
    // Filtres multicritères
    std::string m_filterAuthor;  // Filtre par auteur (vide = pas de filtre)
    std::string m_filterYear;   // Filtre par année (vide = pas de filtre)
    std::vector<std::string> m_availableAuthors;  // Liste des auteurs disponibles
    std::vector<std::string> m_availableYears;    // Liste des années disponibles
    bool m_filtersNeedUpdate;  // Flag pour mettre à jour les listes de filtres
    FilterWidget m_authorFilterWidget;  // Widget de filtre pour les auteurs
    FilterWidget m_yearFilterWidget;    // Widget de filtre pour les années
    bool m_filtersActive;  // True si au moins un filtre est actif (item sélectionné dans la liste)
    std::unordered_map<PlaylistNode*, bool> m_openNodes;  // État d'ouverture des nœuds (pour filtrage dynamique)
    
    // Virtual Scrolling : Liste plate des nœuds visibles
    struct FlatNode {
        PlaylistNode* node;  // Pointeur vers le nœud original
        int depth;           // Profondeur dans l'arbre (pour l'indentation)
        size_t index;        // Index dans la liste plate
    };
    std::vector<FlatNode> m_flatList;      // Liste plate de TOUS les nœuds visibles (structure)
    std::vector<size_t> m_visibleIndices;  // Liste des indices visibles (pour filtrage dynamique)
    bool m_flatListValid;                  // Flag indiquant si la liste plate est valide
    bool m_visibleIndicesValid;            // Flag indiquant si la liste d'indices est valide
    
    // Cache pour renderPlaylistNavigation() (évite de recalculer à chaque frame)
    std::vector<PlaylistNode*> m_cachedAllFiles;  // Liste mise en cache des fichiers
    int m_cachedCurrentIndex;  // Index courant mis en cache (-1 = invalide)
    bool m_navigationCacheValid;  // Flag indiquant si le cache est valide
    
    // Méthodes de rendu
    void renderMainPanel();
    void renderPlayerTab();
    void renderConfigTab();
    void renderPlaylistPanel();
    void renderExplorerTab();  // Onglet Explorer (ancien renderPlaylistPanel)
    void renderHistoryTab();    // Onglet History
    void renderFileBrowser();
    
    // Composants UI
    void renderOscilloscopes();
    void renderPlayerControls();
    void renderPlaylistTree();
    void renderPlaylistNavigation();
    void renderFilters();  // Afficher les filtres multicritères
    bool renderStarRating(const char* label, int* rating, int max_stars = 5);  // Widget de notation par étoiles
    
    // Helpers
    void renderBackground();
    void updateSearchResults();  // Mettre à jour les résultats de recherche fuzzy (max 10)
    void navigateToFile(const std::string& filepath);  // Naviguer vers un fichier dans l'arbre
    void updateFilterLists();  // Mettre à jour les listes d'auteurs et d'années disponibles
    bool matchesFilters(PlaylistNode* node) const;  // Vérifier si un nœud correspond aux filtres
    bool hasVisibleChildren(PlaylistNode* node) const;
    
    // Expand/Collapse tous les nœuds
    void expandAllNodes();
    void collapseAllNodes();
    
    // Virtual Scrolling
    void buildFlatList();              // Construire la liste plate des nœuds visibles
    void buildVisibleIndices();        // Construire la liste des indices visibles (filtrage dynamique)
    void invalidateFlatList();         // Invalider la liste plate (quand ouverture/fermeture change)
    void invalidateVisibleIndices();   // Invalider la liste d'indices (quand filtres changent)
    
    void recordHistoryEntry(const std::string& filepath);  // Enregistrer une entrée dans l'historique
    void invalidateNavigationCache();  // Invalider le cache de navigation (appelé quand playlist/filtres changent)
};

#endif // UI_MANAGER_H

