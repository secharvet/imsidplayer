#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <string>

#ifdef ENABLE_CLOUD_SAVE

class UpdateChecker {
public:
    struct UpdateInfo {
        std::string version;           // Version distante (ex: "1.0.1")
        std::string tagName;           // Tag GitHub (ex: "v1.0.1")
        std::string downloadUrl;        // URL du ZIP à télécharger
        std::string releaseNotes;       // Notes de version
        bool available = false;        // Une mise à jour est disponible
        std::string error;             // Message d'erreur si échec
    };
    
    // Vérifier si une mise à jour est disponible
    // owner: nom d'utilisateur/organisation GitHub (ex: "secharvet")
    // repo: nom du dépôt GitHub (ex: "imsidplayer")
    // currentVersion: version locale (ex: "1.0.0")
    static UpdateInfo checkForUpdate(const std::string& currentVersion,
                                     const std::string& owner = "secharvet",
                                     const std::string& repo = "imsidplayer");
    
    // Comparer deux versions (format "X.Y.Z")
    // Retourne true si remoteVersion > localVersion
    static bool isNewerVersion(const std::string& localVersion, 
                               const std::string& remoteVersion);
    
    // Obtenir le suffixe de plateforme pour l'archive
    // Retourne "windows" ou "linux"
    static std::string getPlatformSuffix();
    
    // Obtenir l'extension d'archive selon la plateforme
    // Retourne ".zip" pour Windows, ".tar.gz" pour Linux
    static std::string getArchiveExtension();

private:
    // Parser une version (supporte "v1.0.0" ou "1.0.0")
    static std::tuple<int, int, int> parseVersion(const std::string& version);
};

#endif // ENABLE_CLOUD_SAVE

#endif // UPDATE_CHECKER_H
