/*
 * UIManager_AccountDialogs.cpp
 * Gestion des dialogues de compte (Cloud Save)
 */

#include "UIManager.h"
#include "Application.h"
#include "Config.h"
#include "Logger.h"
#include "imgui.h"
#include "PopupManager.h"

#ifdef ENABLE_CLOUD_SAVE

// --- Helpers Standardisés ---

bool UIManager::beginCenteredModal(const char* name) {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    // Forcer une largeur fixe pour la cohérence, hauteur auto
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(MODAL_WIDTH, 0.0f), 
        ImVec2(MODAL_WIDTH, 1000.0f)
    );
    
    bool open = ImGui::BeginPopupModal(name, nullptr, 
        ImGuiWindowFlags_AlwaysAutoResize | 
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);
        
    if (open) {
        // Activer le wrapping automatique pour tout le contenu textuel
        ImGui::PushTextWrapPos(ImGui::GetCursorStartPos().x + MODAL_WIDTH - 20.0f);
    }
    
    return open;
}

void UIManager::renderModalFooter(const char* cancelLabel, std::function<void()> onCancel, 
                                 const char* confirmLabel, std::function<void()> onConfirm, 
                                 bool confirmDisabled, bool isDestructive) {
    // Restaurer le wrap pos (fermer celui ouvert dans beginCenteredModal)
    ImGui::PopTextWrapPos();
    
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Calculer la largeur des boutons pour qu'ils remplissent l'espace
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    // 2 boutons -> (Largeur - 1 espace) / 2
    float btnWidth = (availableWidth - spacing) / 2.0f;
    
    // Bouton Annuler
    if (ImGui::Button(cancelLabel, ImVec2(btnWidth, MODAL_BUTTON_HEIGHT)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (onCancel) onCancel();
    }
    
    ImGui::SameLine();
    
    // Bouton Confirmer (avec style destructif optionnel)
    if (isDestructive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
    }
    
    ImGui::BeginDisabled(confirmDisabled);
    // Gestion de la touche Entrée pour confirmer (si non désactivé)
    // Note: ImGui::Button ne capture pas Entrée par défaut sauf focus, on peut ajouter un check manuel si besoin
    if (ImGui::Button(confirmLabel, ImVec2(btnWidth, MODAL_BUTTON_HEIGHT))) {
        if (onConfirm) onConfirm();
    }
    ImGui::EndDisabled();
    
    if (isDestructive) {
        ImGui::PopStyleColor(3);
    }
    
    ImGui::EndPopup();
}

// --- Implémentations des Dialogues ---

void UIManager::renderAccountDialogs() {
    if (!m_popupManager) return;
    
    PopupManager::PopupType currentPopup = m_popupManager->getCurrentPopup();
    
    switch (currentPopup) {
        case PopupManager::PopupType::AccountSetup:
            if (m_accountDialogState == AccountDialogState::StartupChoice) {
                renderStartupAccountDialog();
            }
            break;
        case PopupManager::PopupType::CreateAccount:
            if (!m_supabaseClient) return;
            // Ne pas réinitialiser l'état si on est en train de transitionner vers la clé de récupération (succès)
            if (m_accountDialogState != AccountDialogState::Creating && 
                m_accountDialogState != AccountDialogState::ShowingRecoveryKey) {
                m_accountDialogState = AccountDialogState::Creating;
                m_usernameInput[0] = '\0';
                m_accountError.clear();
                m_usernameCheckInProgress = false;
                m_usernameAvailable = false;
                m_usernameChecked = false;
            }
            renderCreateAccountDialog();
            break;
        case PopupManager::PopupType::RecoverAccount:
            if (!m_supabaseClient) return;
            if (m_accountDialogState == AccountDialogState::Recovering) {
                renderRecoverAccountDialog();
            }
            break;
        case PopupManager::PopupType::RecoveryKey:
            if (m_accountDialogState == AccountDialogState::ShowingRecoveryKey) {
                renderRecoveryKeyDialog();
            }
            break;
        case PopupManager::PopupType::DeleteAccountConfirmation:
            if (m_accountDialogState != AccountDialogState::DeleteConfirmation) {
                m_accountDialogState = AccountDialogState::DeleteConfirmation;
                m_accountError.clear();
            }
            renderDeleteAccountConfirmation();
            break;
        case PopupManager::PopupType::PublishRatingsConfirmation:
            if (m_accountDialogState != AccountDialogState::PublishConfirmation) {
                m_accountDialogState = AccountDialogState::PublishConfirmation;
                m_accountError.clear();
            }
            renderPublishRatingsConfirmation();
            break;
        default:
            break;
    }
}

void UIManager::renderStartupAccountDialog() {
    // Vérification contextuelle
    if (!m_popupManager || m_popupManager->getCurrentPopup() != PopupManager::PopupType::AccountSetup) return;
    
    // Le popup AccountSetup a 3 choix, donc renderModalFooter standard ne suffit pas tout à fait,
    // ou alors on adapte. On va le faire manuellement mais avec le style beginCenteredModal.
    
    if (beginCenteredModal("Account Setup")) {
        ImGui::Text("Welcome to imSidPlayer!");
        ImGui::Spacing();
        ImGui::TextUnformatted("To use community ratings, you need to set up an account.");
        ImGui::Spacing();
        
        if (!m_supabaseClient) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Warning: Supabase credentials not configured.");
            ImGui::TextUnformatted("Please configure 'supabase_project_url' and 'supabase_anon_key' in Settings > Config before creating an account.");
            ImGui::Spacing();
        }
        
        // Custom Footer pour 3 boutons
        ImGui::PopTextWrapPos(); // Fin du wrap
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // 3 boutons verticaux ou horizontaux ? Verticaux est plus lisible pour des choix longs
        float availW = ImGui::GetContentRegionAvail().x;
        
        ImGui::BeginDisabled(!m_supabaseClient);
        if (ImGui::Button("Create New Account", ImVec2(availW, MODAL_BUTTON_HEIGHT))) {
            if (m_supabaseClient) {
                m_accountDialogState = AccountDialogState::Creating;
                m_usernameInput[0] = '\0';
                m_accountError.clear();
                // Reset flags
                m_usernameCheckInProgress = false;
                m_usernameAvailable = false;
                m_usernameChecked = false;
                
                ImGui::CloseCurrentPopup();
                m_popupManager->queuePopup(PopupManager::PopupType::CreateAccount, true);
                m_popupManager->notifyPopupClosed();
            }
        }
        
        ImGui::Spacing();
        
        if (ImGui::Button("Recover Existing Account", ImVec2(availW, MODAL_BUTTON_HEIGHT))) {
            if (m_supabaseClient) {
                m_accountDialogState = AccountDialogState::Recovering;
                m_recoveryCodeInput[0] = '\0';
                m_accountError.clear();
                
                ImGui::CloseCurrentPopup();
                m_popupManager->queuePopup(PopupManager::PopupType::RecoverAccount, true);
                m_popupManager->notifyPopupClosed();
            }
        }
        ImGui::EndDisabled();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("Skip", ImVec2(availW, MODAL_BUTTON_HEIGHT))) {
            m_accountDialogState = AccountDialogState::None;
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
        }
        
        ImGui::EndPopup();
    }
}

void UIManager::renderCreateAccountDialog() {
    if (!m_popupManager || m_popupManager->getCurrentPopup() != PopupManager::PopupType::CreateAccount) return;
    
    if (beginCenteredModal("Create Account")) {
        ImGui::Text("Create a new account");
        ImGui::Spacing();
        ImGui::Text("Enter a username for your account:");
        ImGui::Spacing();
        
        // Input prend toute la largeur
        ImGui::PushItemWidth(-1);
        bool enterPressed = ImGui::InputText("##username", m_usernameInput, sizeof(m_usernameInput), 
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            m_usernameChecked = false;
            m_usernameAvailable = false;
            m_accountError.clear();
        }
        
        ImGui::Spacing();
        
        // Bouton Check Availability
        ImGui::BeginDisabled(strlen(m_usernameInput) == 0 || m_usernameCheckInProgress || m_accountOperationInProgress);
        if (ImGui::Button("Check Availability", ImVec2(-1, MODAL_BUTTON_HEIGHT))) {
            // Logique de check (inchangée)
            std::string username(m_usernameInput);
            m_usernameCheckInProgress = true;
            m_accountError.clear();
            m_usernameChecked = false;
            m_usernameAvailable = false;
            
            std::thread([this, username]() {
                if (!m_supabaseClient) return;
                bool available = m_supabaseClient->isUsernameAvailable(username);
                m_usernameAvailable = available;
                m_usernameChecked = true;
                m_usernameCheckInProgress = false;
                if (!available) {
                    std::string error = m_supabaseClient->getLastError();
                    m_accountError = error.empty() ? "Username taken" : error;
                }
            }).detach();
        }
        ImGui::EndDisabled();
        
        // Status
        if (m_usernameCheckInProgress) {
            ImGui::Text("Checking...");
        } else if (m_usernameChecked) {
            if (m_usernameAvailable) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Available");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Not available");
            }
        }
        
        if (!m_accountError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_accountError.c_str());
        }
        
        if (m_accountOperationInProgress) {
            ImGui::Spacing();
            ImGui::Text("Creating account...");
        }
        
        bool canCreate = strlen(m_usernameInput) > 0 && m_usernameChecked && m_usernameAvailable && !m_accountOperationInProgress;
        
        // Actions
        auto onCancel = [this]() {
            m_accountDialogState = AccountDialogState::StartupChoice;
            m_accountError.clear();
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
        };
        
        auto onConfirm = [this]() {
            // Logique de création (inchangée, juste encapsulée)
            std::string username(m_usernameInput);
            m_accountOperationInProgress = true;
            m_accountError.clear();
            
            std::thread([this, username]() {
                if (!m_supabaseClient) return;
                
                auto authResponse = m_supabaseClient->signInAnonymously();
                if (!authResponse.success) {
                    m_accountError = authResponse.error_message;
                    m_accountOperationInProgress = false;
                    return;
                }
                
                if (!m_supabaseClient->updateUsername(username)) {
                    m_accountError = m_supabaseClient->getLastError();
                    m_accountOperationInProgress = false;
                    return;
                }
                
                Config& config = Config::getInstance();
                config.setCommunityRatingsUsername(username);
                config.save();
                
                auto recoveryResponse = m_supabaseClient->generateRecoveryCode();
                if (!recoveryResponse.success) {
                    m_accountError = recoveryResponse.error_message;
                    m_accountOperationInProgress = false;
                    return;
                }
                
                config.setRecoveryCode(recoveryResponse.recovery_code);
                config.save();
                
                m_recoveryKeyDisplay = recoveryResponse.recovery_code;
                m_accountOperationInProgress = false;
                m_accountDialogState = AccountDialogState::ShowingRecoveryKey;
                
                m_popupManager->queuePopup(PopupManager::PopupType::RecoveryKey, true);
            }).detach();
        };
        
        // Check manuel pour Enter key sur l'input ou bouton
        if (enterPressed && canCreate) {
            onConfirm();
        }
        
        // Check si transition vers RecoveryKey nécessaire (thread terminé)
        if (m_accountDialogState == AccountDialogState::ShowingRecoveryKey && !m_accountOperationInProgress) {
             ImGui::CloseCurrentPopup();
             m_popupManager->notifyPopupClosed();
             // Le footer ne sera pas rendu, mais le EndPopup du helper doit l'être.
             // Hack: on doit appeler PopTextWrapPos et EndPopup nous-même si on sort prématurément ?
             // Non, on laisse renderModalFooter le faire, mais comme on sort ici...
             // On doit sortir de la fonction.
             ImGui::PopTextWrapPos();
             ImGui::EndPopup();
             return;
        }

        renderModalFooter("Cancel", onCancel, "Create Account", onConfirm, !canCreate);
    }
}

void UIManager::renderRecoverAccountDialog() {
    if (!m_popupManager || m_popupManager->getCurrentPopup() != PopupManager::PopupType::RecoverAccount) return;
    
    if (beginCenteredModal("Recover Account")) {
        ImGui::Text("Recover an existing account");
        ImGui::Spacing();
        ImGui::Text("Enter your Recovery Key:");
        ImGui::Spacing();
        
        ImGui::PushItemWidth(-1);
        bool enterPressed = ImGui::InputText("##recovery_code", m_recoveryCodeInput, sizeof(m_recoveryCodeInput), 
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        if (!m_accountError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_accountError.c_str());
        }
        
        if (m_accountOperationInProgress) {
            ImGui::Spacing();
            ImGui::Text("Recovering...");
        }
        
        bool canRecover = strlen(m_recoveryCodeInput) > 0 && !m_accountOperationInProgress;
        
        auto onCancel = [this]() {
            m_accountDialogState = AccountDialogState::StartupChoice;
            m_accountError.clear();
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
        };
        
        auto onConfirm = [this]() {
            std::string recoveryCode(m_recoveryCodeInput);
            m_accountOperationInProgress = true;
            m_accountError.clear();
            
            std::thread([this, recoveryCode]() {
                if (!m_supabaseClient) return;
                auto authResponse = m_supabaseClient->recoverAccountWithCode(recoveryCode);
                if (authResponse.success) {
                    LOG_INFO("Account recovered successfully");
                    
                    // Persister le recovery code dans la config (réutilisable pour autres appareils)
                    Config& config = Config::getInstance();
                    config.setRecoveryCode(recoveryCode);
                    config.save();
                    m_recoveryKeyDisplay = recoveryCode;
                    
                    // Fetch ratings from cloud immediately
                    LOG_INFO("Fetching ratings from cloud...");
                    std::vector<CommunityRating> ratings;
                    if (m_supabaseClient->getAllMyRatings(ratings)) {
                        LOG_INFO("Fetched {} ratings from cloud", ratings.size());
                        int imported = 0;
                        for (const auto& r : ratings) {
                            // Find song by MD5 hash
                            const SidMetadata* meta = m_database.getMetadataByMD5(r.file_hash);
                            if (meta && !meta->filepath.empty()) {
                                // Update local rating
                                m_ratingManager.updateRating(meta->metadataHash, r.rating);
                                imported++;
                            }
                        }
                        LOG_INFO("Imported {} ratings to local database", imported);
                    } else {
                        LOG_WARNING("Failed to fetch ratings from cloud: {}", m_supabaseClient->getLastError());
                    }

                    m_accountDialogState = AccountDialogState::None;
                } else {
                    m_accountError = authResponse.error_message;
                }
                m_accountOperationInProgress = false;
            }).detach();
        };
        
        if (enterPressed && canRecover) {
            onConfirm();
        }
        
        // Auto-close si succès
        if (m_accountDialogState == AccountDialogState::None && !m_accountOperationInProgress && m_accountError.empty()) {
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
            return;
        }
        
        renderModalFooter("Cancel", onCancel, "Recover Account", onConfirm, !canRecover);
    }
}

void UIManager::renderRecoveryKeyDialog() {
    if (!m_popupManager || m_popupManager->getCurrentPopup() != PopupManager::PopupType::RecoveryKey) return;
    
    if (beginCenteredModal("Recovery Key")) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Account created successfully!");
        ImGui::Spacing();
        ImGui::TextUnformatted("Please save this Recovery Key in a safe place. You will need it to recover your account on another device.");
        ImGui::Spacing();
        
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##recovery_key_display", const_cast<char*>(m_recoveryKeyDisplay.c_str()), 
                        m_recoveryKeyDisplay.size() + 1, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopItemWidth();
        ImGui::PopFont();
        
        auto onConfirm = [this]() {
            m_accountDialogState = AccountDialogState::None;
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
        };
        
        // On n'a pas besoin de bouton Cancel ici, donc on passe une lambda vide ou on modifie le helper
        // Pour faire simple, on utilise renderModalFooter mais on mettra "Close" comme confirm et on ignorera cancel
        // Ou mieux : on fait un footer custom simple
        
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("I have saved my key", ImVec2(-1, MODAL_BUTTON_HEIGHT))) {
            onConfirm();
        }
        
        ImGui::EndPopup();
    }
}

void UIManager::renderDeleteAccountConfirmation() {
    if (!m_popupManager || m_popupManager->getCurrentPopup() != PopupManager::PopupType::DeleteAccountConfirmation) return;
    
    // Suivi de l'action pour éviter la fermeture prématurée
    static bool s_deleteActionStarted = false;
    
    if (beginCenteredModal("Delete Account Confirmation")) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: This action is irreversible!");
        ImGui::Spacing();
        ImGui::TextUnformatted("Are you sure you want to delete your account? All your ratings will be permanently lost.");
        ImGui::Spacing();
        
        if (!m_accountError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_accountError.c_str());
            ImGui::Spacing();
        }
        
        if (m_accountOperationInProgress) {
            ImGui::Text("Deleting account...");
        }
        
        auto onCancel = [this]() {
            m_accountDialogState = AccountDialogState::None;
            m_accountError.clear();
            s_deleteActionStarted = false;
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
        };
        
        auto onConfirm = [this]() {
            m_accountOperationInProgress = true;
            m_accountError.clear();
            s_deleteActionStarted = true;
            
            std::thread([this]() {
                if (m_supabaseClient) {
                    m_supabaseClient->signOut();
                }
                
                Config& config = Config::getInstance();
                config.setSupabaseAccessToken("");
                config.setSupabaseRefreshToken("");
                config.setSupabaseUserId("");
                config.setCommunityRatingsUsername("");
                config.setRecoveryCode("");
                config.save();
                
                LOG_INFO("[UIManager_AccountDialogs] Account deleted locally");
                
                m_accountOperationInProgress = false;
                m_accountDialogState = AccountDialogState::None;
            }).detach();
        };
        
        // Auto-close uniquement si on a lancé l'action et qu'elle est finie
        if (s_deleteActionStarted && !m_accountOperationInProgress && m_accountError.empty()) {
             ImGui::CloseCurrentPopup();
             m_popupManager->notifyPopupClosed();
             ImGui::PopTextWrapPos();
             ImGui::EndPopup();
             s_deleteActionStarted = false;
             return;
        }
        
        renderModalFooter("Cancel", onCancel, "Delete Account", onConfirm, m_accountOperationInProgress, true);
    }
}

void UIManager::renderPublishRatingsConfirmation() {
    if (!m_popupManager || m_popupManager->getCurrentPopup() != PopupManager::PopupType::PublishRatingsConfirmation) return;
    
    static bool s_publishActionStarted = false;
    
    if (beginCenteredModal("Publish Ratings")) {
        ImGui::Text("Are you sure you want to publish your ratings?");
        ImGui::Spacing();
        ImGui::TextUnformatted("Your ratings will be shared with the community. This action cannot be undone (but you can update them later).");
        ImGui::Spacing();
        ImGui::TextWrapped("Only files with ratings (1-5 stars) will be synced.");
        ImGui::Spacing();
        
        if (m_accountOperationInProgress) {
            ImGui::Text("Publishing... (this may take a while)");
        }
        
        if (!m_accountError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_accountError.c_str());
            ImGui::Spacing();
        }
        
        auto onCancel = [this]() {
            m_accountDialogState = AccountDialogState::None;
            m_accountError.clear();
            m_operationStatus.clear();
            s_publishActionStarted = false;
            ImGui::CloseCurrentPopup();
            m_popupManager->notifyPopupClosed();
        };
        
        auto onConfirm = [this]() {
            m_accountOperationInProgress = true;
            m_accountError.clear();
            s_publishActionStarted = true;
            
            std::thread([this]() {
                if (!m_supabaseClient) return;
                
                // 1. Récupérer les ratings locaux
                std::map<uint32_t, RatingManager::InternalData> localData = m_ratingManager.getAllData();
                std::vector<CommunityRating> ratingsToSync;
                
                // 2. Convertir et filtrer
                for (const auto& [hash, data] : localData) {
                    if (data.rating > 0) {
                        const SidMetadata* meta = m_database.getMetadataByHash(hash);
                        if (meta && !meta->md5Hash.empty()) {
                            CommunityRating r;
                            r.file_hash = meta->md5Hash;
                            // r.filepath n'existe plus
                            r.rating = data.rating;
                            ratingsToSync.push_back(r);
                        }
                    }
                }
                
                LOG_INFO("Found {} ratings to sync out of {} local entries.", ratingsToSync.size(), localData.size());
                
                if (ratingsToSync.empty()) {
                    m_accountError = "No ratings found to publish.";
                    m_accountOperationInProgress = false;
                    return;
                }
                
                // 3. Envoyer
                if (m_supabaseClient->syncRatingsToCloud(ratingsToSync)) {
                    LOG_INFO("Ratings published successfully.");
                    m_operationStatus = std::to_string(ratingsToSync.size()) + " ratings successfully published.";
                    m_accountError.clear();
                    m_accountDialogState = AccountDialogState::None;
                } else {
                    m_accountError = "Failed to publish ratings.";
                    std::string err = m_supabaseClient->getLastError();
                    if (!err.empty()) {
                        m_accountError += " " + err;
                    }
                    m_operationStatus.clear();
                }
                
                m_accountOperationInProgress = false;
            }).detach();
        };
        
        // Auto-close sur succès
        if (s_publishActionStarted && !m_accountOperationInProgress && m_accountError.empty()) {
             ImGui::CloseCurrentPopup();
             m_popupManager->notifyPopupClosed();
             ImGui::PopTextWrapPos();
             ImGui::EndPopup();
             s_publishActionStarted = false;
             return;
        }
        
        renderModalFooter("Cancel", onCancel, "Publish", onConfirm, m_accountOperationInProgress);
    }
}

#endif // ENABLE_CLOUD_SAVE
