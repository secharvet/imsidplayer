#ifndef UTILS_H
#define UTILS_H

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Fonction helper pour obtenir le répertoire de configuration (~/.imsidplayer/)
fs::path getConfigDir();

// Calculer le hash MD5 d'un fichier (pour Songlengths.md5)
std::string calculateFileMD5(const std::string& filepath);

// Convertir une chaîne Latin-1 vers UTF-8
// Les fichiers SID utilisent souvent Latin-1 (ISO-8859-1) au lieu d'UTF-8
std::string latin1ToUtf8(const std::string& latin1);

#endif // UTILS_H



