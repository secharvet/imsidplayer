#ifndef UPDATE_INSTALLER_H
#define UPDATE_INSTALLER_H

#ifdef ENABLE_CLOUD_SAVE

#include <string>

class UpdateInstaller {
public:
    // Télécharger et installer une mise à jour
    // downloadUrl: URL du ZIP à télécharger
    // version: Version à installer (pour logging)
    // Retourne true si succès, false sinon
    static bool installUpdate(const std::string& downloadUrl, const std::string& version);
    
    // Obtenir le chemin de l'exécutable actuel
    static std::string getExecutablePath();
    
    // Obtenir le répertoire de l'exécutable
    static std::string getExecutableDirectory();
    
    // Supprimer les fichiers .old au démarrage (appelé dans Application::initialize())
    static void cleanupOldFiles();

private:
    // Télécharger un fichier depuis une URL
    static bool downloadFile(const std::string& url, const std::string& targetPath);
    
    // Extraire un ZIP (solution simple avec std::filesystem + zlib si disponible)
    // Pour l'instant, on suppose que le ZIP contient juste l'exe
    static bool extractZip(const std::string& zipPath, const std::string& destDir);
    
    // Renommer l'exe actuel en .old
    static bool renameCurrentExecutable();
    
    // Copier le nouvel exe depuis le dossier temporaire
    static bool copyNewExecutable(const std::string& tempDir);
};

#endif // ENABLE_CLOUD_SAVE

#endif // UPDATE_INSTALLER_H
