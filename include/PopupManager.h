#ifndef POPUP_MANAGER_H
#define POPUP_MANAGER_H

#ifdef ENABLE_CLOUD_SAVE

#include <deque>
#include <functional>
#include <string>

// Gestionnaire de popups simplifié
// Utilise une file d'attente (Deque) pour gérer l'ordre d'affichage
// Permet d'insérer des popups en priorité (au début) pour les enchainements
class PopupManager {
public:
    enum class PopupType {
        None,
        AccountSetup,
        CreateAccount,
        RecoverAccount,
        RecoveryKey,
        DeleteAccountConfirmation,
        PublishRatingsConfirmation,
        UpdateAvailable
    };

    PopupManager();
    
    // Ajouter un popup à la file
    // highPriority = true : Ajoute au DÉBUT de la file (pour enchaîner immédiatement après le popup courant)
    // highPriority = false : Ajoute à la FIN de la file (pour les événements moins urgents)
    void queuePopup(PopupType type, bool highPriority = false);
    
    // Appelé quand le popup actuel se ferme
    void notifyPopupClosed();
    
    // État
    PopupType getCurrentPopup() const { return m_currentPopup; }
    size_t getQueueSize() const { return m_popupQueue.size(); }
    bool hasPendingPopups() const { return !m_popupQueue.empty(); }
    bool hasPendingPopupsOfType(PopupType type) const;
    
    // Callback de rendu
    using RenderCallback = std::function<void(PopupType)>;
    void setRenderCallback(RenderCallback callback) { m_renderCallback = callback; }
    
    // Boucle principale de gestion (à appeler à chaque frame)
    void render();
    
private:
    std::deque<PopupType> m_popupQueue;  // File d'attente (double-ended queue)
    PopupType m_currentPopup;            // Popup actuellement affiché
    RenderCallback m_renderCallback;
    
    void showNext();                     // Affiche le prochain popup de la file
    void openCurrentPopupImGui();        // Ouvre le popup ImGui correspondant à m_currentPopup
    bool isCurrentPopupOpen() const;     // Vérifie l'état ImGui
};

#endif // ENABLE_CLOUD_SAVE

#endif // POPUP_MANAGER_H
