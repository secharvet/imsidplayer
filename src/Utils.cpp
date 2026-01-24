#include "Utils.h"
#include "MD5.h"
#include "Logger.h"
#include <cstdlib>
#include <stdexcept>
#include <fstream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

fs::path getConfigDir() {
    fs::path homeDir;
    
#ifdef _WIN32
    // Windows: utiliser APPDATA ou USERPROFILE
    const char* appdata = getenv("APPDATA");
    if (appdata) {
        homeDir = fs::path(appdata);
    } else {
        const char* userprofile = getenv("USERPROFILE");
        if (userprofile) {
            homeDir = fs::path(userprofile);
        }
    }
#else
    // Unix/Linux: utiliser HOME ou getpwuid
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    if (home) {
        homeDir = fs::path(home);
    }
#endif
    
    if (homeDir.empty()) {
        return fs::current_path() / ".imsidplayer";
    }
    
    fs::path configDir = homeDir / ".imsidplayer";
    if (!fs::exists(configDir)) {
        try {
            fs::create_directories(configDir);
        } catch (const std::exception& e) {
            LOG_WARNING("Impossible de créer le répertoire de configuration: {}, utilisation du répertoire courant", e.what());
            return fs::current_path() / ".imsidplayer";
        }
    }
    return configDir;
}

std::string calculateFileMD5(const std::string& filepath) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            LOG_WARNING("Cannot open file for MD5 calculation: {}", filepath);
            return "";
        }
        
        imsid::ImSidMD5 md5;
        std::vector<unsigned char> buffer(4096);
        
        while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size())) {
            md5.update(buffer.data(), file.gcount());
        }
        
        if (file.gcount() > 0) {
            md5.update(buffer.data(), file.gcount());
        }
        
        md5.finalize();
        return md5.toString();
    } catch (const std::exception& e) {
        LOG_WARNING("Error calculating MD5 for {}: {}", filepath, e.what());
        return "";
    }
}

