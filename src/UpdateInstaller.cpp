#ifdef ENABLE_CLOUD_SAVE

#include "UpdateInstaller.h"
#include "HTTPClient.h"
#include "Logger.h"
#include "Utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

std::string UpdateInstaller::getExecutablePath() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return "";
    }
    return std::string(path);
#else
    char path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", path, PATH_MAX - 1);
    if (length == -1) {
        return "";
    }
    path[length] = '\0';
    return std::string(path);
#endif
}

std::string UpdateInstaller::getExecutableDirectory() {
    std::string exePath = getExecutablePath();
    if (exePath.empty()) {
        return fs::current_path().string();
    }
    return fs::path(exePath).parent_path().string();
}

void UpdateInstaller::cleanupOldFiles() {
    std::string exeDir = getExecutableDirectory();
    if (exeDir.empty()) return;
    
    try {
        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.ends_with(".old")) {
                    LOG_INFO("Suppression du fichier .old : {}", filename);
                    fs::remove(entry.path());
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARNING("Erreur lors du nettoyage des fichiers .old : {}", e.what());
    }
}

bool UpdateInstaller::downloadFile(const std::string& url, const std::string& targetPath) {
    LOG_INFO("Téléchargement de {} vers {}", url, targetPath);
    
    HTTPClient client;
    if (!client.initialize()) {
        LOG_ERROR("Échec de l'initialisation du client HTTP");
        return false;
    }
    
    // Charger les certificats CA
    std::string caPath = "certs/cacert.pem";
    if (!client.loadRootCA(caPath)) {
        LOG_WARNING("Impossible de charger les certificats CA, continuation quand même");
    }
    
    // Télécharger le fichier
    auto response = client.get(url);
    client.cleanup();
    
    if (response.statusCode != 200) {
        LOG_ERROR("Échec du téléchargement : code HTTP {}", response.statusCode);
        return false;
    }
    
    // Sauvegarder dans le fichier cible
    try {
        fs::path target(targetPath);
        fs::create_directories(target.parent_path());
        
        std::ofstream file(targetPath, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("Impossible d'ouvrir le fichier pour écriture : {}", targetPath);
            return false;
        }
        
        file.write(response.body.data(), response.body.size());
        file.close();
        
        LOG_INFO("Fichier téléchargé avec succès : {} ({} bytes)", targetPath, response.body.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors de l'écriture du fichier : {}", e.what());
        return false;
    }
}

bool UpdateInstaller::extractZip(const std::string& zipPath, const std::string& destDir) {
    // Pour l'instant, solution simple : on suppose que le ZIP contient juste l'exe
    // TODO: Implémenter l'extraction ZIP complète avec minizip-ng
    LOG_WARNING("Extraction ZIP non implémentée, copie directe du fichier");
    
    // Solution temporaire : si le ZIP est en fait juste l'exe (cas simple)
    // On copie directement
    try {
        fs::path zip(zipPath);
        fs::path dest(destDir);
        fs::create_directories(dest);
        
        // Pour l'instant, on suppose que le fichier téléchargé est directement l'exe
        // Dans un vrai cas, il faudrait extraire le ZIP
        std::string exeName = "imSidPlayer";
        #ifdef _WIN32
        exeName += ".exe";
        #endif
        
        fs::path targetExe = dest / exeName;
        fs::copy_file(zipPath, targetExe, fs::copy_options::overwrite_existing);
        
        // Rendre exécutable sur Linux
        #ifndef _WIN32
        fs::permissions(targetExe, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                                   fs::perms::group_read | fs::perms::group_exec |
                                   fs::perms::others_read | fs::perms::others_exec);
        #endif
        
        LOG_INFO("Fichier extrait : {}", targetExe.string());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors de l'extraction : {}", e.what());
        return false;
    }
}

bool UpdateInstaller::renameCurrentExecutable() {
    std::string exePath = getExecutablePath();
    if (exePath.empty()) {
        LOG_ERROR("Impossible de déterminer le chemin de l'exécutable");
        return false;
    }
    
    std::string oldPath = exePath + ".old";
    
    try {
        // Supprimer l'ancien .old s'il existe
        if (fs::exists(oldPath)) {
            fs::remove(oldPath);
        }
        
        // Renommer l'exe actuel
        fs::rename(exePath, oldPath);
        LOG_INFO("Exécutable renommé : {} -> {}", exePath, oldPath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors du renommage : {}", e.what());
        return false;
    }
}

bool UpdateInstaller::copyNewExecutable(const std::string& tempDir) {
    std::string exePath = getExecutablePath();
    if (exePath.empty()) {
        LOG_ERROR("Impossible de déterminer le chemin de l'exécutable");
        return false;
    }
    
    std::string exeName = fs::path(exePath).filename().string();
    fs::path sourceExe = fs::path(tempDir) / exeName;
    
    if (!fs::exists(sourceExe)) {
        LOG_ERROR("Fichier source introuvable : {}", sourceExe.string());
        return false;
    }
    
    try {
        fs::copy_file(sourceExe, exePath, fs::copy_options::overwrite_existing);
        
        // Rendre exécutable sur Linux
        #ifndef _WIN32
        fs::permissions(exePath, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                                 fs::perms::group_read | fs::perms::group_exec |
                                 fs::perms::others_read | fs::perms::others_exec);
        #endif
        
        LOG_INFO("Nouvel exécutable copié : {}", exePath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors de la copie : {}", e.what());
        return false;
    }
}

bool UpdateInstaller::installUpdate(const std::string& downloadUrl, const std::string& version) {
    LOG_INFO("Début de l'installation de la mise à jour {}", version);
    
    std::string exeDir = getExecutableDirectory();
    if (exeDir.empty()) {
        LOG_ERROR("Impossible de déterminer le répertoire de l'exécutable");
        return false;
    }
    
    // Créer un dossier temporaire pour le téléchargement
    fs::path tempDir = fs::path(exeDir) / "update_temp";
    try {
        fs::create_directories(tempDir);
    } catch (const std::exception& e) {
        LOG_ERROR("Impossible de créer le dossier temporaire : {}", e.what());
        return false;
    }
    
    // Télécharger le ZIP
    std::string zipPath = (tempDir / "update.zip").string();
    if (!downloadFile(downloadUrl, zipPath)) {
        LOG_ERROR("Échec du téléchargement");
        return false;
    }
    
    // Extraire le ZIP
    std::string extractDir = (tempDir / "extracted").string();
    if (!extractZip(zipPath, extractDir)) {
        LOG_ERROR("Échec de l'extraction");
        return false;
    }
    
    // Renommer l'exe actuel en .old
    if (!renameCurrentExecutable()) {
        LOG_ERROR("Échec du renommage de l'exécutable actuel");
        return false;
    }
    
    // Copier le nouvel exe
    if (!copyNewExecutable(extractDir)) {
        LOG_ERROR("Échec de la copie du nouvel exécutable");
        // TODO: Restaurer l'ancien exe depuis .old en cas d'échec
        return false;
    }
    
    // Nettoyer le dossier temporaire
    try {
        fs::remove_all(tempDir);
    } catch (const std::exception& e) {
        LOG_WARNING("Impossible de supprimer le dossier temporaire : {}", e.what());
    }
    
    LOG_INFO("Mise à jour installée avec succès : {}", version);
    return true;
}

#endif // ENABLE_CLOUD_SAVE
