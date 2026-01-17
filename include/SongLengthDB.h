#ifndef SONGLENGTH_DB_H
#define SONGLENGTH_DB_H

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * Classe singleton pour gérer la base de données Songlengths.md5
 * 
 * Format du fichier Songlengths.md5 :
 * - Première ligne : [Database]
 * - Lignes commentées avec ; pour le chemin du fichier
 * - Format : md5_hash=durée1 durée2 durée3 ...
 * - Durées au format mm:ss ou mm:ss.SSS (en secondes)
 * 
 * Exemple :
 * [Database]
 * ; /DEMOS/0-9/12345.sid
 * 2727236ead44a62f0c6e01f6dd4dc484=0:56
 */
class SongLengthDB {
public:
    static SongLengthDB& getInstance();
    
    // Charger la base de données depuis un fichier
    bool load(const std::string& filepath);
    
    // Obtenir les durées pour un hash MD5 donné (retourne un vecteur de durées en secondes)
    // Retourne un vecteur vide si le hash n'existe pas
    std::vector<double> getDurations(const std::string& md5Hash) const;
    
    // Obtenir la durée d'un subsong spécifique (index 0-based)
    // Retourne -1.0 si le hash ou l'index n'existe pas
    double getDuration(const std::string& md5Hash, size_t subsongIndex = 0) const;
    
    // Vérifier si un hash existe dans la base
    bool hasHash(const std::string& md5Hash) const;
    
    // Obtenir le chemin du fichier actuellement chargé
    std::string getFilePath() const { return m_filepath; }
    
    // Vérifier si la base est chargée
    bool isLoaded() const { return !m_filepath.empty() && !m_database.empty(); }
    
    // Obtenir le nombre d'entrées dans la base
    size_t getCount() const { return m_database.size(); }
    
    // Vider la base de données
    void clear();
    
private:
    SongLengthDB() = default;
    ~SongLengthDB() = default;
    SongLengthDB(const SongLengthDB&) = delete;
    SongLengthDB& operator=(const SongLengthDB&) = delete;
    
    // Convertir une durée au format "mm:ss" ou "mm:ss.SSS" en secondes
    static double parseDuration(const std::string& durationStr);
    
    // Vérifier le format d'un fichier Songlengths.md5
    static bool isValidFormat(const std::string& filepath);
    
    std::unordered_map<std::string, std::vector<double>> m_database; // md5 -> vector de durées (secondes)
    std::string m_filepath; // Chemin du fichier chargé
};

#endif // SONGLENGTH_DB_H




