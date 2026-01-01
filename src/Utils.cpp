#include "Utils.h"
#include "Logger.h"
#include <cstdlib>
#include <stdexcept>
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

