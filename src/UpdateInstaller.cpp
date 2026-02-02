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
#include <sys/wait.h>
#include <sys/types.h>
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
    try {
        fs::path archive(zipPath);
        fs::path dest(destDir);
        fs::create_directories(dest);
        
        std::string archiveExt = archive.extension().string();
        std::transform(archiveExt.begin(), archiveExt.end(), archiveExt.begin(), ::tolower);
        
        #ifdef _WIN32
        // Windows: extraction ZIP avec PowerShell
        if (archiveExt == ".zip") {
            LOG_INFO("Extraction ZIP : {} vers {}", archive.string(), dest.string());
            
            // Utiliser PowerShell Expand-Archive pour extraire le ZIP
            // Échapper correctement les chemins pour PowerShell
            std::string archivePath = archive.string();
            std::string destPath = dest.string();
            
            // Remplacer les backslashes par des doubles backslashes pour l'échappement PowerShell
            std::string escapedArchive;
            std::string escapedDest;
            for (char c : archivePath) {
                if (c == '\\') {
                    escapedArchive += "\\\\";
                } else if (c == '\'') {
                    escapedArchive += "''";
                } else {
                    escapedArchive += c;
                }
            }
            for (char c : destPath) {
                if (c == '\\') {
                    escapedDest += "\\\\";
                } else if (c == '\'') {
                    escapedDest += "''";
                } else {
                    escapedDest += c;
                }
            }
            
            std::string psCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path '";
            psCommand += escapedArchive;
            psCommand += "' -DestinationPath '";
            psCommand += escapedDest;
            psCommand += "' -Force\"";
            
            LOG_DEBUG("Commande PowerShell : {}", psCommand);
            
            int result = system(psCommand.c_str());
            if (result != 0) {
                LOG_ERROR("Échec de l'extraction ZIP (code: {})", result);
                return false;
            }
            
            LOG_INFO("Archive ZIP extraite avec succès dans : {}", dest.string());
            return true;
        } else {
            LOG_ERROR("Format d'archive non supporté sur Windows : {}", archiveExt);
            return false;
        }
        #else
        // Linux: extraction tar.gz avec tar (commande système standard)
        // Utilisation de fork+exec au lieu de system() pour plus de sécurité
        if (archiveExt == ".gz" || archive.string().ends_with(".tar.gz")) {
            LOG_INFO("Extraction tar.gz : {} vers {}", archive.string(), dest.string());
            
            pid_t pid = fork();
            if (pid == -1) {
                LOG_ERROR("Échec de fork() pour l'extraction tar.gz");
                return false;
            }
            
            if (pid == 0) {
                // Processus enfant : exécuter tar
                std::string archiveStr = archive.string();
                std::string destStr = dest.string();
                
                // Utiliser execvp pour éviter l'injection de commande
                const char* args[] = {
                    "tar",
                    "-xzf",
                    archiveStr.c_str(),
                    "-C",
                    destStr.c_str(),
                    nullptr
                };
                
                execvp("tar", const_cast<char* const*>(args));
                // Si execvp échoue, on sort avec un code d'erreur
                _exit(1);
            } else {
                // Processus parent : attendre la fin de l'extraction
                int status;
                waitpid(pid, &status, 0);
                
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    LOG_INFO("Archive tar.gz extraite avec succès dans : {}", dest.string());
                    return true;
                } else {
                    LOG_ERROR("Échec de l'extraction tar.gz (code: {})", WEXITSTATUS(status));
                    return false;
                }
            }
        } else {
            LOG_ERROR("Format d'archive non supporté : {}", archiveExt);
            return false;
        }
        #endif
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

bool UpdateInstaller::copyNewExecutable(const std::string& tempDir, const std::string& targetPath) {
    if (targetPath.empty()) {
        LOG_ERROR("Chemin de destination vide");
        return false;
    }
    
    fs::path exePath(targetPath);
    
    fs::path extractDir(tempDir);
    fs::path sourceExe;
    
    // Chercher l'exécutable dans le dossier extrait
    // Sur Linux, il s'appelle "imSidPlayer", sur Windows "imSidPlayer.exe"
    #ifdef _WIN32
    std::string exeName = "imSidPlayer.exe";
    #else
    std::string exeName = "imSidPlayer";
    #endif
    
    // Essayer d'abord directement dans le dossier extrait
    sourceExe = extractDir / exeName;
    
    // Si pas trouvé, chercher récursivement dans le dossier
    if (!fs::exists(sourceExe)) {
        LOG_DEBUG("Fichier {} non trouvé, recherche récursive...", sourceExe.string());
        bool found = false;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(extractDir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    #ifdef _WIN32
                    if (filename == exeName || filename == "imSidPlayer.exe") {
                    #else
                    if (filename == exeName || filename == "imSidPlayer") {
                    #endif
                        sourceExe = entry.path();
                        found = true;
                        LOG_DEBUG("Exécutable trouvé : {}", sourceExe.string());
                        break;
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Erreur lors de la recherche récursive : {}", e.what());
        }
        
        if (!found) {
            LOG_ERROR("Fichier exécutable introuvable dans : {}", extractDir.string());
            // Lister les fichiers pour debug
            try {
                LOG_DEBUG("Contenu du dossier extrait :");
                for (const auto& entry : fs::directory_iterator(extractDir)) {
                    LOG_DEBUG("  - {} ({})", entry.path().filename().string(), 
                             entry.is_directory() ? "dossier" : "fichier");
                }
            } catch (...) {}
            return false;
        }
    }
    
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
        
        LOG_INFO("Nouvel exécutable copié : {} <- {}", exePath.string(), sourceExe.string());
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
    
    // Télécharger l'archive (ZIP pour Windows, tar.gz pour Linux)
    std::string archivePath;
    #ifdef _WIN32
    archivePath = (tempDir / "update.zip").string();
    #else
    archivePath = (tempDir / "update.tar.gz").string();
    #endif
    
    if (!downloadFile(downloadUrl, archivePath)) {
        LOG_ERROR("Échec du téléchargement");
        return false;
    }
    
    // Extraire l'archive
    std::string extractDir = (tempDir / "extracted").string();
    if (!extractZip(archivePath, extractDir)) {
        LOG_ERROR("Échec de l'extraction");
        return false;
    }
    
    // Obtenir le chemin de l'exécutable AVANT de le renommer
    // (car getExecutablePath() pourrait pointer vers .old après renommage)
    std::string targetExePath = getExecutablePath();
    if (targetExePath.empty()) {
        LOG_ERROR("Impossible de déterminer le chemin de l'exécutable");
        return false;
    }
    
    // Renommer l'exe actuel en .old
    if (!renameCurrentExecutable()) {
        LOG_ERROR("Échec du renommage de l'exécutable actuel");
        return false;
    }
    
    // Copier le nouvel exe vers le chemin original (pas .old)
    if (!copyNewExecutable(extractDir, targetExePath)) {
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
