#include "SongLengthDB.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

SongLengthDB& SongLengthDB::getInstance() {
    static SongLengthDB instance;
    return instance;
}

bool SongLengthDB::isValidFormat(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    // Vérifier que la première ligne est [Database]
    std::string firstLine;
    if (!std::getline(file, firstLine)) {
        return false;
    }
    
    // Supprimer les espaces en début/fin
    firstLine.erase(0, firstLine.find_first_not_of(" \t\r\n"));
    firstLine.erase(firstLine.find_last_not_of(" \t\r\n") + 1);
    
    if (firstLine != "[Database]") {
        return false;
    }
    
    // Vérifier qu'il y a au moins une ligne de données valide (md5=durée)
    std::regex md5Regex("^[0-9a-fA-F]{32}=.*$");
    std::string line;
    bool foundValidLine = false;
    
    while (std::getline(file, line)) {
        // Ignorer les lignes vides et les commentaires
        if (line.empty() || line[0] == ';') {
            continue;
        }
        
        // Supprimer les espaces
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Vérifier le format md5=durée
        if (std::regex_match(line, md5Regex)) {
            foundValidLine = true;
            break;
        }
    }
    
    return foundValidLine;
}

double SongLengthDB::parseDuration(const std::string& durationStr) {
    // Format : mm:ss ou mm:ss.SSS
    // Exemples : "0:56", "1:02", "3:57", "1:02.5", "1:02.500"
    
    size_t colonPos = durationStr.find(':');
    if (colonPos == std::string::npos) {
        return -1.0; // Format invalide
    }
    
    try {
        // Extraire les minutes
        std::string minutesStr = durationStr.substr(0, colonPos);
        int minutes = std::stoi(minutesStr);
        
        // Extraire les secondes (avec ou sans millisecondes)
        std::string secondsStr = durationStr.substr(colonPos + 1);
        size_t dotPos = secondsStr.find('.');
        
        double seconds;
        if (dotPos == std::string::npos) {
            // Pas de millisecondes
            seconds = std::stoi(secondsStr);
        } else {
            // Avec millisecondes
            std::string secPart = secondsStr.substr(0, dotPos);
            std::string msecPart = secondsStr.substr(dotPos + 1);
            
            int sec = std::stoi(secPart);
            int msec = std::stoi(msecPart);
            
            // Convertir millisecondes en secondes (support jusqu'à 3 chiffres)
            double msecSeconds = msec;
            if (msecPart.length() == 1) {
                msecSeconds = msec / 10.0; // 1 chiffre = dixièmes
            } else if (msecPart.length() == 2) {
                msecSeconds = msec / 100.0; // 2 chiffres = centièmes
            } else {
                msecSeconds = msec / 1000.0; // 3 chiffres = millisecondes
            }
            
            seconds = sec + msecSeconds;
        }
        
        return minutes * 60.0 + seconds;
    } catch (...) {
        return -1.0; // Erreur de parsing
    }
}

bool SongLengthDB::load(const std::string& filepath) {
    // Vérifier que le fichier existe
    if (!fs::exists(filepath)) {
        LOG_ERROR("Songlengths.md5 file not found: {}", filepath);
        return false;
    }
    
    // Vérifier le format
    if (!isValidFormat(filepath)) {
        LOG_ERROR("Invalid Songlengths.md5 format: {}", filepath);
        return false;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open Songlengths.md5 file: {}", filepath);
        return false;
    }
    
    clear();
    m_filepath = filepath;
    
    std::string line;
    bool foundDatabaseHeader = false;
    size_t lineNumber = 0;
    size_t entriesLoaded = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        
        // Supprimer les espaces en début/fin
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Ignorer les lignes vides
        if (line.empty()) {
            continue;
        }
        
        // Vérifier le header [Database]
        if (line == "[Database]") {
            foundDatabaseHeader = true;
            continue;
        }
        
        if (!foundDatabaseHeader) {
            continue; // Ignorer les lignes avant le header
        }
        
        // Ignorer les commentaires (lignes commençant par ;)
        if (line[0] == ';') {
            continue;
        }
        
        // Parser une ligne md5=durée1 durée2 ...
        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) {
            continue; // Format invalide, ignorer
        }
        
        std::string md5Hash = line.substr(0, equalPos);
        std::string durationsStr = line.substr(equalPos + 1);
        
        // Vérifier que le hash MD5 est valide (32 caractères hexadécimaux)
        if (md5Hash.length() != 32) {
            LOG_WARNING("Invalid MD5 hash length at line {}: {}", lineNumber, md5Hash);
            continue;
        }
        
        // Convertir le hash en minuscules pour normalisation
        std::transform(md5Hash.begin(), md5Hash.end(), md5Hash.begin(), ::tolower);
        
        // Vérifier que tous les caractères sont hexadécimaux
        bool validHash = true;
        for (char c : md5Hash) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                validHash = false;
                break;
            }
        }
        
        if (!validHash) {
            LOG_WARNING("Invalid MD5 hash format at line {}: {}", lineNumber, md5Hash);
            continue;
        }
        
        // Parser les durées
        std::vector<double> durations;
        std::istringstream durationsStream(durationsStr);
        std::string durationStr;
        
        while (durationsStream >> durationStr) {
            double duration = parseDuration(durationStr);
            if (duration >= 0.0) {
                durations.push_back(duration);
            } else {
                LOG_WARNING("Invalid duration format at line {}: {}", lineNumber, durationStr);
            }
        }
        
        if (!durations.empty()) {
            m_database[md5Hash] = durations;
            entriesLoaded++;
        }
    }
    
    LOG_INFO("Songlengths.md5 loaded: {} entries from {}", entriesLoaded, filepath);
    return true;
}

std::vector<double> SongLengthDB::getDurations(const std::string& md5Hash) const {
    // Normaliser le hash (minuscules)
    std::string normalizedHash = md5Hash;
    std::transform(normalizedHash.begin(), normalizedHash.end(), normalizedHash.begin(), ::tolower);
    
    auto it = m_database.find(normalizedHash);
    if (it != m_database.end()) {
        return it->second;
    }
    
    return {}; // Vecteur vide si non trouvé
}

double SongLengthDB::getDuration(const std::string& md5Hash, size_t subsongIndex) const {
    std::vector<double> durations = getDurations(md5Hash);
    
    if (subsongIndex < durations.size()) {
        return durations[subsongIndex];
    }
    
    return -1.0; // Index invalide ou hash non trouvé
}

bool SongLengthDB::hasHash(const std::string& md5Hash) const {
    std::string normalizedHash = md5Hash;
    std::transform(normalizedHash.begin(), normalizedHash.end(), normalizedHash.begin(), ::tolower);
    
    return m_database.find(normalizedHash) != m_database.end();
}

void SongLengthDB::clear() {
    m_database.clear();
    m_filepath.clear();
}

