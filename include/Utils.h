#ifndef UTILS_H
#define UTILS_H

#include <filesystem>

namespace fs = std::filesystem;

// Fonction helper pour obtenir le répertoire de configuration (~/.imsidplayer/)
fs::path getConfigDir();

#endif // UTILS_H

