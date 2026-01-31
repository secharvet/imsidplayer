#ifdef ENABLE_CLOUD_SAVE

#include "UpdateChecker.h"
#include "HTTPClient.h"
#include "Logger.h"
#include "Version.h"
#include <glaze/glaze.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <tuple>

// Structure pour parser la réponse JSON de l'API GitHub
struct GitHubAsset {
    std::string name;
    std::string browser_download_url;
    
    struct glaze {
        using T = GitHubAsset;
        static constexpr auto value = glz::object(
            "name", &T::name,
            "browser_download_url", &T::browser_download_url
        );
    };
};

struct GitHubRelease {
    std::string tag_name;
    std::string name;
    std::string body;  // Release notes
    std::vector<GitHubAsset> assets;
    bool draft = false;  // Pour filtrer les drafts
    bool prerelease = false;  // Pour filtrer les prereleases
    
    struct glaze {
        using T = GitHubRelease;
        static constexpr auto value = glz::object(
            "tag_name", &T::tag_name,
            "name", &T::name,
            "body", &T::body,
            "assets", &T::assets,
            "draft", &T::draft,
            "prerelease", &T::prerelease
        );
    };
};

UpdateChecker::UpdateInfo UpdateChecker::checkForUpdate(
    const std::string& currentVersion,
    const std::string& owner,
    const std::string& repo) {
    
    UpdateInfo info;
    info.available = false;
    
    // Construire l'URL de l'API GitHub
    // Essayer d'abord /releases/latest, puis /releases si ça échoue
    std::string apiUrl = "https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest";
    
    LOG_INFO("Checking for updates: {}", apiUrl);
    
    // Initialiser HTTPClient
    HTTPClient client;
    if (!client.initialize()) {
        info.error = "Failed to initialize HTTP client: " + client.getLastError();
        LOG_ERROR("Update check failed: {}", info.error);
        return info;
    }
    
    // Charger les certificats CA (même méthode que CloudSyncManager)
    std::string caPath = "certs/cacert.pem";
    if (!client.loadRootCA(caPath)) {
        LOG_WARNING("Failed to load CA certificates from {}, continuing anyway", caPath);
    }
    
    // Faire la requête GET
    auto response = client.get(apiUrl);
    
    LOG_DEBUG("Update check response: status={}, body length={}", response.statusCode, response.body.length());
    if (response.body.length() < 500) {
        LOG_DEBUG("Update check response body: {}", response.body);
    } else {
        LOG_DEBUG("Update check response body (first 500 chars): {}", response.body.substr(0, 500));
    }
    
    // Si /releases/latest retourne 404, essayer /releases et prendre la première non-draft
    if (response.statusCode == 404) {
        LOG_DEBUG("/releases/latest returned 404, trying /releases instead");
        apiUrl = "https://api.github.com/repos/" + owner + "/" + repo + "/releases";
        response = client.get(apiUrl);
        
        LOG_DEBUG("Update check /releases response: status={}, body length={}", response.statusCode, response.body.length());
        if (response.body.length() < 500) {
            LOG_DEBUG("Update check /releases response body: {}", response.body);
        } else {
            LOG_DEBUG("Update check /releases response body (first 500 chars): {}", response.body.substr(0, 500));
        }
        
        if (response.statusCode == 200) {
            // Parser la liste de releases
            // Utiliser opts pour ignorer les clés inconnues (GitHub renvoie beaucoup de champs)
            std::vector<GitHubRelease> releases;
            auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(releases, response.body);
            
            if (error) {
                std::string errorMsg = glz::format_error(error, response.body);
                LOG_ERROR("Failed to parse releases JSON: {}", errorMsg);
                info.error = "Failed to parse releases JSON: " + errorMsg;
                return info;
            }
            
            LOG_DEBUG("Parsed {} releases from JSON", releases.size());
            
            if (!releases.empty()) {
                // Trouver la première release non-draft et non-prerelease
                GitHubRelease* latestRelease = nullptr;
                for (auto& r : releases) {
                    if (!r.draft && !r.prerelease) {
                        latestRelease = &r;
                        break;
                    }
                }
                
                // Si aucune release non-draft trouvée, prendre la première
                if (!latestRelease && !releases.empty()) {
                    latestRelease = &releases[0];
                }
                
                if (latestRelease) {
                    // Traiter directement cette release (pas besoin de réécrire le JSON)
                    GitHubRelease& release = *latestRelease;
                    
                    // Extraire la version du tag (enlever le "v" si présent)
                    std::string remoteVersion = release.tag_name;
                    if (!remoteVersion.empty() && remoteVersion[0] == 'v') {
                        remoteVersion = remoteVersion.substr(1);
                    }
                    
                    info.tagName = release.tag_name;
                    info.version = remoteVersion;
                    info.releaseNotes = release.body;
                    
                    // Vérifier si la version distante est plus récente
                    if (!isNewerVersion(currentVersion, remoteVersion)) {
                        LOG_INFO("No update available. Current: {}, Latest: {}", currentVersion, remoteVersion);
                        return info;
                    }
                    
                    // Trouver l'asset correspondant à la plateforme
                    std::string platformSuffix = getPlatformSuffix();
                    std::string archiveExt = getArchiveExtension();
                    
                    LOG_DEBUG("Looking for asset with platform suffix: '{}' and archive extension: '{}'", platformSuffix, archiveExt);
                    
                    for (const auto& asset : release.assets) {
                        std::string nameLower = asset.name;
                        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                        
                        LOG_DEBUG("Checking asset: '{}'", asset.name);
                        
                        // Pour Linux, le fichier n'a pas d'extension, il se termine par "-linux"
                        // Pour Windows, on cherche ".zip"
                        bool matchesPlatform = nameLower.find(platformSuffix) != std::string::npos;
                        bool matchesExtension = false;
                        
                        if (platformSuffix == "linux") {
                            // Linux: le fichier doit se terminer par "-linux" (pas d'extension)
                            matchesExtension = nameLower.ends_with("-linux") || nameLower.ends_with("-linux.tar.gz");
                        } else {
                            // Windows/Mac: chercher l'extension d'archive
                            matchesExtension = nameLower.find(archiveExt) != std::string::npos;
                        }
                        
                        if (matchesPlatform && matchesExtension) {
                            info.downloadUrl = asset.browser_download_url;
                            info.available = true;
                            LOG_INFO("Update available: {} -> {} ({})", currentVersion, remoteVersion, info.downloadUrl);
                            return info;
                        }
                    }
                    
                    // Si aucun asset trouvé pour cette plateforme
                    info.error = "No asset found for platform: " + platformSuffix;
                    LOG_WARNING("Update available but no asset for platform {}: {}", platformSuffix, remoteVersion);
                    return info;
                } else {
                    response.statusCode = 404;
                }
            } else {
                response.statusCode = 404;
            }
        }
    }
    
    client.cleanup();
    
    if (response.statusCode != 200) {
        info.error = "HTTP error: " + std::to_string(response.statusCode);
        LOG_ERROR("Update check failed: {}", info.error);
        return info;
    }
    
    // Parser le JSON avec glaze (cas normal: /releases/latest a fonctionné)
    // Utiliser opts pour ignorer les clés inconnues (GitHub renvoie beaucoup de champs)
    GitHubRelease release;
    auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(release, response.body);
    
    if (error) {
        std::string errorMsg = glz::format_error(error, response.body);
        info.error = "Failed to parse JSON response: " + errorMsg;
        LOG_ERROR("Update check failed: failed to parse JSON - {}", errorMsg);
        LOG_DEBUG("Response body that failed to parse: {}", response.body);
        return info;
    }
    
    // Extraire la version du tag (enlever le "v" si présent)
    std::string remoteVersion = release.tag_name;
    if (!remoteVersion.empty() && remoteVersion[0] == 'v') {
        remoteVersion = remoteVersion.substr(1);
    }
    
    info.tagName = release.tag_name;
    info.version = remoteVersion;
    info.releaseNotes = release.body;
    
    // Vérifier si la version distante est plus récente
    if (!isNewerVersion(currentVersion, remoteVersion)) {
        LOG_INFO("No update available. Current: {}, Latest: {}", currentVersion, remoteVersion);
        return info;
    }
    
    // Trouver l'asset correspondant à la plateforme
    std::string platformSuffix = getPlatformSuffix();
    std::string archiveExt = getArchiveExtension();
    
    LOG_DEBUG("Looking for asset with platform suffix: '{}' and archive extension: '{}'", platformSuffix, archiveExt);
    
    for (const auto& asset : release.assets) {
        std::string nameLower = asset.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        
        LOG_DEBUG("Checking asset: '{}'", asset.name);
        
        // Pour Linux, chercher .tar.gz
        // Pour Windows, chercher .zip
        bool matchesPlatform = nameLower.find(platformSuffix) != std::string::npos;
        bool matchesExtension = false;
        
        if (platformSuffix == "linux") {
            // Linux: chercher .tar.gz
            matchesExtension = nameLower.ends_with(".tar.gz") || nameLower.ends_with("-linux.tar.gz");
        } else {
            // Windows/Mac: chercher l'extension d'archive
            matchesExtension = nameLower.find(archiveExt) != std::string::npos;
        }
        
        if (matchesPlatform && matchesExtension) {
            info.downloadUrl = asset.browser_download_url;
            info.available = true;
            LOG_INFO("Update available: {} -> {} ({})", currentVersion, remoteVersion, info.downloadUrl);
            return info;
        }
    }
    
    // Si aucun asset trouvé pour cette plateforme
    info.error = "No asset found for platform: " + platformSuffix;
    LOG_WARNING("Update available but no asset for platform {}: {}", platformSuffix, remoteVersion);
    return info;
}

bool UpdateChecker::isNewerVersion(const std::string& localVersion, const std::string& remoteVersion) {
    auto [lMaj, lMin, lPat] = parseVersion(localVersion);
    auto [rMaj, rMin, rPat] = parseVersion(remoteVersion);
    
    if (rMaj > lMaj) return true;
    if (rMaj < lMaj) return false;
    
    if (rMin > lMin) return true;
    if (rMin < lMin) return false;
    
    return rPat > lPat;
}

std::tuple<int, int, int> UpdateChecker::parseVersion(const std::string& version) {
    int major = 0, minor = 0, patch = 0;
    
    // Enlever le "v" si présent
    std::string clean = version;
    if (!clean.empty() && clean[0] == 'v') {
        clean = clean.substr(1);
    }
    
    // Parser "X.Y.Z"
    std::istringstream iss(clean);
    std::string token;
    
    if (std::getline(iss, token, '.')) {
        try { major = std::stoi(token); } catch (...) {}
    }
    if (std::getline(iss, token, '.')) {
        try { minor = std::stoi(token); } catch (...) {}
    }
    if (std::getline(iss, token, '.')) {
        try { patch = std::stoi(token); } catch (...) {}
    }
    
    return {major, minor, patch};
}

std::string UpdateChecker::getPlatformSuffix() {
    #ifdef _WIN32
        return "windows";
    #elif __linux__
        return "linux";
    #elif __APPLE__
        return "macos";
    #else
        return "unknown";
    #endif
}

std::string UpdateChecker::getArchiveExtension() {
    #ifdef _WIN32
        return ".zip";
    #elif __linux__
        return ".tar.gz";
    #elif __APPLE__
        return ".dmg";
    #else
        return "";
    #endif
}

#endif // ENABLE_CLOUD_SAVE
