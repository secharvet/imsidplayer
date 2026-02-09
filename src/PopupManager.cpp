#ifdef ENABLE_CLOUD_SAVE

#include "PopupManager.h"
#include "imgui.h"
#include "Logger.h"
#include <algorithm>

PopupManager::PopupManager() 
    : m_currentPopup(PopupType::None)
{
    LOG_DEBUG("[PopupManager] Initialized (Simple Deque Mode)");
}

void PopupManager::queuePopup(PopupType type, bool highPriority) {
    LOG_INFO("[PopupManager] queuePopup called for type: {}, priority: {}", static_cast<int>(type), highPriority);
    
    if (type == PopupType::None) return;
    
    // Éviter les doublons immédiats
    if (m_currentPopup == type) {
        LOG_INFO("[PopupManager] Ignored queue request for {} (already current)", static_cast<int>(type));
        return;
    }
    
    // Vérifier si déjà dans la queue
    for (const auto& popup : m_popupQueue) {
        if (popup == type) return;
    }

    if (highPriority) {
        m_popupQueue.push_front(type);
        LOG_DEBUG("[PopupManager] 📥 QUEUE FRONT: {} (Size: {})", static_cast<int>(type), m_popupQueue.size());
    } else {
        m_popupQueue.push_back(type);
        LOG_DEBUG("[PopupManager] 📥 QUEUE BACK: {} (Size: {})", static_cast<int>(type), m_popupQueue.size());
    }
}

bool PopupManager::hasPendingPopupsOfType(PopupType type) const {
    for (const auto& popup : m_popupQueue) {
        if (popup == type) return true;
    }
    return false;
}

void PopupManager::notifyPopupClosed() {
    // On marque simplement qu'aucun popup n'est actif.
    // Le prochain sera pris en charge au début du prochain appel à render().
    // Cela évite les conflits d'état dans la même frame.
    if (m_currentPopup != PopupType::None) {
        LOG_DEBUG("[PopupManager] 🔔 CLOSED: {} -> None", static_cast<int>(m_currentPopup));
        m_currentPopup = PopupType::None;
    }
}

void PopupManager::showNext() {
    if (m_currentPopup != PopupType::None || m_popupQueue.empty()) return;

    m_currentPopup = m_popupQueue.front();
    m_popupQueue.pop_front();
    
    LOG_DEBUG("[PopupManager] ▶️ OPENING: {} (Remaining: {})", static_cast<int>(m_currentPopup), m_popupQueue.size());

    openCurrentPopupImGui();
}

void PopupManager::openCurrentPopupImGui() {
    switch (m_currentPopup) {
        case PopupType::AccountSetup: ImGui::OpenPopup("Account Setup"); break;
        case PopupType::CreateAccount: ImGui::OpenPopup("Create Account"); break;
        case PopupType::RecoverAccount: ImGui::OpenPopup("Recover Account"); break;
        case PopupType::RecoveryKey: ImGui::OpenPopup("Recovery Key"); break;
        case PopupType::DeleteAccountConfirmation: ImGui::OpenPopup("Delete Account Confirmation"); break;
        case PopupType::PublishRatingsConfirmation: ImGui::OpenPopup("Publish Ratings"); break;
        case PopupType::UpdateAvailable: ImGui::OpenPopup("Mise à jour disponible"); break;
        default: break;
    }
}

bool PopupManager::isCurrentPopupOpen() const {
    if (m_currentPopup == PopupType::None) return false;
    
    switch (m_currentPopup) {
        case PopupType::AccountSetup: return ImGui::IsPopupOpen("Account Setup");
        case PopupType::CreateAccount: return ImGui::IsPopupOpen("Create Account");
        case PopupType::RecoverAccount: return ImGui::IsPopupOpen("Recover Account");
        case PopupType::RecoveryKey: return ImGui::IsPopupOpen("Recovery Key");
        case PopupType::DeleteAccountConfirmation: return ImGui::IsPopupOpen("Delete Account Confirmation");
        case PopupType::PublishRatingsConfirmation: return ImGui::IsPopupOpen("Publish Ratings");
        case PopupType::UpdateAvailable: return ImGui::IsPopupOpen("Mise à jour disponible");
        default: return false;
    }
}

void PopupManager::render() {
    // 1. GESTION DES TRANSITIONS
    // Si aucun popup n'est actif et qu'il y en a en attente, on lance le suivant.
    if (m_currentPopup == PopupType::None && !m_popupQueue.empty()) {
        showNext();
    }

    // 2. RENDU
    if (m_currentPopup != PopupType::None) {
        // Sécurité : On s'assure que le flag OpenPopup est bien mis pour ImGui
        // (ImGui reset parfois ce flag si le popup ne s'affiche pas une frame)
        if (!isCurrentPopupOpen()) {
            openCurrentPopupImGui();
        }

        // Appel du callback de rendu (qui contient BeginPopupModal)
        if (m_renderCallback) {
            m_renderCallback(m_currentPopup);
        }
        
        // 3. DÉTECTION DE FERMETURE EXTERNE
        // Si après le rendu, ImGui considère le popup fermé, c'est qu'il a été fermé
        // par l'utilisateur (clic dehors, touche Échap) ou par un appel interne.
        // Note : isCurrentPopupOpen() vérifie le flag interne d'ImGui.
        if (!isCurrentPopupOpen()) {
             LOG_INFO("[PopupManager] 🔴 Popup closed detected by ImGui check (isCurrentPopupOpen() returned false). Current: {}", static_cast<int>(m_currentPopup));
             notifyPopupClosed();
        }
    }
}

#endif // ENABLE_CLOUD_SAVE
