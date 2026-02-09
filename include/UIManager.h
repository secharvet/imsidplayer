#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "SidPlayer.h"
#include "PlaylistManager.h"
#include "BackgroundManager.h"
#include "FileBrowser.h"
#include "DatabaseManager.h"
#include "HistoryManager.h"
#include "RatingManager.h"
#include "FilterWidget.h"
#include "Logger.h"
#ifdef ENABLE_CLOUD_SAVE
#include "PopupManager.h"
#endif
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>

// Macros de logging conditionnelles pour UIManager
// Définir ENABLE_UI_LOGS lors de la compilation pour activer les logs UI
#ifdef ENABLE_UI_LOGS
    #define UI_LOG_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
    #define UI_LOG_INFO(...) LOG_INFO(__VA_ARGS__)
    #define UI_LOG_WARNING(...) LOG_WARNING(__VA_ARGS__)
    #define UI_LOG_ERROR(...) LOG_ERROR(__VA_ARGS__)
#else
    #define UI_LOG_DEBUG(...) ((void)0)
    #define UI_LOG_INFO(...) ((void)0)
    #define UI_LOG_WARNING(...) ((void)0)
    #define UI_LOG_ERROR(...) ((void)0)
#endif

#ifdef ENABLE_CLOUD_SAVE
class SupabaseClient;
#endif

class UIManager {
public:
    UIManager(SidPlayer& player, PlaylistManager& playlist, BackgroundManager& background, FileBrowser& fileBrowser, DatabaseManager& database, HistoryManager& history, RatingManager& ratingManager);
    
#ifdef ENABLE_CLOUD_SAVE
    // Définir la référence au SupabaseClient (appelé depuis Application)
    void setSupabaseClient(SupabaseClient* client) { m_supabaseClient = client; }
    
    // Accès au PopupManager
    PopupManager* getPopupManager() { return m_popupManager.get(); }
    const PopupManager* getPopupManager() const { return m_popupManager.get(); }
#endif
    
    // Initialiser ImGui (polices, styles)
    bool initialize(SDL_Window* window, SDL_Renderer* renderer);
    
    // Rendu d'une frame
    void render();
    
    // Gestion des événements
    bool handleEvent(const SDL_Event& event);
    
    // Nettoyage
    void shutdown();
    
    // Gestion du renderer (pour récupération après perte de contexte)
    void shutdownRenderer();
    bool reinitializeRenderer(SDL_Renderer* renderer);
    
    // Getters/Setters
    bool isConfigTabActive() const { return m_isConfigTabActive; }
    bool& showFileDialog() { return m_showFileDialog; }
    std::string& selectedFilePath() { return m_selectedFilePath; }
    bool& indexRequested() { return m_indexRequested; }
    
    // Reconstruire le cache filepath -> metadataHash (appelé après indexation)
    void rebuildFilepathToHashCache();
    
    // Marquer que les filtres doivent être mis à jour (quand la playlist change)
    void markFiltersNeedUpdate() { m_filtersNeedUpdate = true; }
    
    // Rafraîchir l'arbre de la playlist (appelé après ajout de fichiers/dossiers)
    void refreshPlaylistTree();
    
    // Définir les FPS actuels pour affichage
    void setCurrentFPS(float fps) { m_currentFPS = fps; }
    
    // Définir le callback pour rendre le dialog de mise à jour (appelé juste avant ImGui::Render())
    void setUpdateDialogCallback(std::function<void()> callback) { m_updateDialogCallback = callback; }
    
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
    RatingManager& m_ratingManager;
    
    // État des opérations de base de données (mis à jour depuis Application)
    bool m_databaseOperationInProgress;
    std::string m_databaseOperationStatus;
    float m_databaseOperationProgress;
    
    // Callback pour rendre le dialog de mise à jour (appelé juste avant ImGui::Render())
    std::function<void()> m_updateDialogCallback;
    
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    ImFont* m_boldFont;  // Police bold pour les dossiers
    float m_currentFPS;  // FPS actuels pour affichage
    
    // Timings des oscilloscopes pour la fenêtre de debug
    float m_oscilloscopeTime;  // Temps total des oscilloscopes (en ms)
    float m_oscilloscopePlot0Time;  // Temps du plot 0 (en ms)
    float m_oscilloscopePlot1Time;  // Temps du plot 1 (en ms)
    float m_oscilloscopePlot2Time;  // Temps du plot 2 (en ms)
    
    // Palette arc-en-ciel pour les étoiles (255 couleurs)
    std::vector<ImVec4> m_rainbowPalette;
    int m_rainbowCycleOffset;  // Offset pour le cyclage des couleurs
    void generateRainbowPalette();  // Générer la palette arc-en-ciel
    
    // Variables UI
    bool m_showFileDialog;
    std::string m_selectedFilePath;
    bool m_isConfigTabActive;
    bool m_indexRequested;
    bool m_showDebugWindow;  // Afficher/masquer la fenêtre de debug (toggle avec Alt)
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
    int m_filterRating;         // Filtre par rating (0 = pas de filtre, 1-5 = nombre d'étoiles)
    bool m_filterRatingOperator; // Opérateur de comparaison (true = >=, false = =)
    std::vector<std::string> m_availableAuthors;  // Liste des auteurs disponibles
    std::vector<std::string> m_availableYears;    // Liste des années disponibles
    bool m_filtersNeedUpdate;  // Flag pour mettre à jour les listes de filtres
    FilterWidget m_authorFilterWidget;  // Widget de filtre pour les auteurs
    FilterWidget m_yearFilterWidget;    // Widget de filtre pour les années
    bool m_filtersActive;  // True si au moins un filtre est actif (item sélectionné dans la liste)
    std::unordered_map<PlaylistNode*, bool> m_openNodes;  // État d'ouverture des nœuds (pour filtrage dynamique)
    bool m_shouldFocusPlaylist;  // Flag pour donner le focus à la fenêtre de playlist à la prochaine frame
    
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
    void renderDebugWindow(long long newFrameTime, long long mainPanelTime, long long playlistPanelTime,
                          long long fileBrowserTime, long long imguiRenderTime, long long clearTime,
                          long long backgroundTime, long long renderDrawDataTime, long long presentTime, long long totalFrameTime);
    
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
    
    // Obtenir le prochain fichier dans la liste filtrée (utilise le cache si disponible)
    PlaylistNode* getNextFilteredFile();
    
#ifdef ENABLE_CLOUD_SAVE
public:
    // Dialogs pour la gestion de compte Supabase
    void renderAccountDialogs();  // Rendre tous les dialogs de compte
    void renderStartupAccountDialog();  // Dialog de démarrage (Create/Recover)
    void renderCreateAccountDialog();  // Dialog de création de compte
    void renderRecoverAccountDialog();  // Dialog de récupération
    void renderRecoveryKeyDialog();  // Dialog d'affichage recovery key
    void renderDeleteAccountConfirmation(); // Dialog de confirmation de suppression
    void renderPublishRatingsConfirmation(); // Dialog de confirmation de publication
    
    // Helpers pour l'affichage standardisé des modales
    bool beginCenteredModal(const char* name);
    void renderModalFooter(const char* cancelLabel, std::function<void()> onCancel, 
                          const char* confirmLabel, std::function<void()> onConfirm, 
                          bool confirmDisabled = false, bool isDestructive = false);
    
    // Constantes de style UI
    static constexpr float MODAL_WIDTH = 600.0f;
    static constexpr float MODAL_BUTTON_HEIGHT = 35.0f;
    static constexpr float MODAL_BUTTON_WIDTH = 180.0f;
    
    // État des dialogs
    enum class AccountDialogState {
        None,              // Aucun dialog
        StartupChoice,     // Choix Create/Recover au démarrage
        Creating,         // Création en cours
        Recovering,        // Récupération en cours
        ShowingRecoveryKey, // Affichage recovery key
        DeleteConfirmation,  // Confirmation de suppression
        PublishConfirmation  // Confirmation de publication des ratings
    };
    
    AccountDialogState m_accountDialogState;
    SupabaseClient* m_supabaseClient;  // Pointeur vers SupabaseClient (géré par Application, ne pas delete)
    
    // Variables pour les dialogs
    char m_usernameInput[256];
    char m_recoveryCodeInput[16];
    std::string m_recoveryKeyDisplay;  // Recovery key à afficher
    std::string m_accountError;  // Message d'erreur pour les dialogs
    std::string m_operationStatus; // Statut de l'opération en cours (ex: "Syncing batch 1/5...")
    std::atomic<bool> m_accountOperationInProgress;  // True si opération en cours
    bool m_usernameCheckInProgress;  // True si vérification de username en cours
    bool m_usernameAvailable;  // True si le username est disponible (après vérification)
    bool m_usernameChecked;  // True si le username a été vérifié
    
    // PopupManager - orchestrateur centralisé des popups
    std::unique_ptr<PopupManager> m_popupManager;
    bool m_popupManagerCallbackSet = false;  // Flag pour initialiser le callback une seule fois
#endif
};

#endif // UI_MANAGER_H
