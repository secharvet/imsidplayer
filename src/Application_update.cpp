#ifdef ENABLE_CLOUD_SAVE
void Application::checkForUpdatesAsync() {
    // Lancer la vérification dans un thread séparé pour ne pas bloquer le démarrage
    std::thread updateThread([]() {
        auto updateInfo = UpdateChecker::checkForUpdate(VERSION_STRING);
        
        if (updateInfo.available) {
            LOG_INFO("Nouvelle version disponible : {} (actuellement : {})", 
                     updateInfo.version, VERSION_STRING);
            LOG_INFO("URL de téléchargement : {}", updateInfo.downloadUrl);
            // TODO: Afficher une notification dans l'UI (Phase 4)
        } else if (!updateInfo.error.empty()) {
            LOG_DEBUG("Vérification de mise à jour échouée : {}", updateInfo.error);
        } else {
            LOG_DEBUG("Aucune mise à jour disponible (version actuelle : {})", VERSION_STRING);
        }
    });
    updateThread.detach();  // Détacher le thread pour qu'il s'exécute en arrière-plan
}
#endif
