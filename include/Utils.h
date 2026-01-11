#ifndef UTILS_H
#define UTILS_H

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Fonction helper pour obtenir le répertoire de configuration (~/.imsidplayer/)
fs::path getConfigDir();

// Calculer le hash MD5 d'un fichier (pour Songlengths.md5)
std::string calculateFileMD5(const std::string& filepath);

#endif // UTILS_H



