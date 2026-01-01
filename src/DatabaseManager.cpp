#include "DatabaseManager.h"
#include "Utils.h"
#include "PlaylistManager.h"
#include "Logger.h"
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <glaze/glaze.hpp>

namespace fs = std::filesystem;

DatabaseManager::DatabaseManager() {
    fs::path configDir = getConfigDir();
    m_databasePath = (configDir / "database.json").string();
}

bool DatabaseManager::load() {
    if (!fs::exists(m_databasePath)) {
        LOG_INFO("Base de données n'existe pas encore, création d'une nouvelle base");
        return true; // Pas d'erreur, juste pas de fichier existant
    }
    
    try {
        std::string jsonStr;
        std::ifstream file(m_databasePath);
        if (!file) {
            LOG_ERROR("Impossible d'ouvrir la base de données");
            return false;
        }
        
        file.seekg(0, std::ios::end);
        jsonStr.reserve(file.tellg());
        file.seekg(0, std::ios::beg);
        jsonStr.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        auto error = glz::read_json(m_metadata, jsonStr);
        if (error) {
            std::string errorMsg = glz::format_error(error, jsonStr);
            LOG_ERROR("Erreur lors de la lecture de la base de données: {}", errorMsg);
            return false;
        }
        
        // Reconstruire les index (filepath et metadataHash)
        m_filepathIndex.clear();
        m_hashIndex.clear();
        for (size_t i = 0; i < m_metadata.size(); ++i) {
            m_filepathIndex[m_metadata[i].filepath] = i;
            if (m_metadata[i].metadataHash != 0) {
                m_hashIndex[m_metadata[i].metadataHash] = i;
            }
        }
        
        LOG_INFO("Base de données chargée: {} fichiers indexés", m_metadata.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors du chargement de la base de données: {}", e.what());
        return false;
    }
}

bool DatabaseManager::save() {
    try {
        auto result = glz::write_json(m_metadata);
        if (!result) {
            LOG_ERROR("Erreur lors de l'écriture de la base de données");
            return false;
        }
        std::string jsonStr = result.value();
        
        std::ofstream file(m_databasePath);
        if (!file) {
            LOG_ERROR("Impossible d'écrire dans la base de données");
            return false;
        }
        
        file << jsonStr;
        LOG_INFO("Base de données sauvegardée: {} fichiers", m_metadata.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors de la sauvegarde de la base de données: {}", e.what());
        return false;
    }
}

int DatabaseManager::indexPlaylist(PlaylistManager& playlist, std::function<void(const std::string&, int, int)> progressCallback) {
    auto allFiles = playlist.getAllFiles();
    int indexed = 0;
    int total = allFiles.size();
    
    for (size_t i = 0; i < allFiles.size(); ++i) {
        PlaylistNode* node = allFiles[i];
        if (!node || node->filepath.empty()) continue;
        
        if (progressCallback) {
            progressCallback(node->filepath, i + 1, total);
        }
        
        if (indexFile(node->filepath)) {
            indexed++;
        }
    }
    
    save();
    return indexed;
}

bool DatabaseManager::indexFile(const std::string& filepath) {
    if (!fs::exists(filepath)) {
        return false;
    }
    
    // Extraire les métadonnées d'abord pour obtenir le metadataHash
    SidMetadata metadata = extractMetadata(filepath);
    if (metadata.filepath.empty() || metadata.metadataHash == 0) {
        return false; // Erreur lors de l'extraction
    }
    
    // Vérifier si déjà indexé par metadataHash (clé primaire)
    auto hashIt = m_hashIndex.find(metadata.metadataHash);
    if (hashIt != m_hashIndex.end()) {
        // Le morceau existe déjà dans la base (même métadonnées)
        SidMetadata& existing = m_metadata[hashIt->second];
        
        // Mettre à jour le filepath si différent (fichier déplacé)
        if (existing.filepath != filepath) {
            // Retirer l'ancien filepath de l'index
            m_filepathIndex.erase(existing.filepath);
            // Mettre à jour le filepath et ajouter le nouveau
            existing.filepath = filepath;
            m_filepathIndex[filepath] = hashIt->second;
        }
        
        // Vérifier si le fichier a changé (taille, date)
        if (existing.isFileChanged()) {
            // Réindexer car le fichier a changé
            existing = extractMetadata(filepath);
            if (existing.filepath.empty()) {
                return false;
            }
            return true; // Fichier réindexé
        }
        
        return false; // Déjà indexé et à jour
    }
    
    // Nouveau morceau : ajouter les métadonnées
    size_t index = m_metadata.size();
    m_metadata.push_back(metadata);
    m_filepathIndex[filepath] = index;
    m_hashIndex[metadata.metadataHash] = index;
    
    return true;
}

bool DatabaseManager::isIndexed(const std::string& filepath) const {
    auto it = m_filepathIndex.find(filepath);
    if (it != m_filepathIndex.end()) {
        // Vérifier si le fichier a changé
        const SidMetadata& metadata = m_metadata[it->second];
        return !metadata.isFileChanged();
    }
    return false;
}

bool DatabaseManager::isIndexedByMetadataHash(uint32_t metadataHash) const {
    auto it = m_hashIndex.find(metadataHash);
    return it != m_hashIndex.end();
}

const SidMetadata* DatabaseManager::getMetadata(const std::string& filepath) const {
    auto it = m_filepathIndex.find(filepath);
    if (it != m_filepathIndex.end()) {
        return &m_metadata[it->second];
    }
    return nullptr;
}

const SidMetadata* DatabaseManager::getMetadataByHash(uint32_t metadataHash) const {
    auto it = m_hashIndex.find(metadataHash);
    if (it == m_hashIndex.end()) {
        return nullptr;
    }
    return &m_metadata[it->second];
}

std::unordered_set<uint32_t> DatabaseManager::getIndexedMetadataHashes() const {
    std::unordered_set<uint32_t> hashes;
    for (const auto& pair : m_hashIndex) {
        hashes.insert(pair.first);
    }
    return hashes;
}

std::vector<const SidMetadata*> DatabaseManager::search(const std::string& query) const {
    if (query.empty()) {
        return {};
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<const SidMetadata*> results;
    std::vector<std::pair<const SidMetadata*, double>> scoredResults;
    
    std::string queryLower = query;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
    
    const bool useFuzzy = queryLower.length() >= 3; // Fuzzy seulement si query >= 3 caractères
    const size_t maxResults = 25; // Limité à 25 résultats
    size_t exactMatches = 0;
    
    // Première passe : recherche exacte uniquement (rapide)
    for (const auto& metadata : m_metadata) {
        double score = 0.0;
        bool hasExactMatch = false;
        
        // Recherche dans le titre (exact match)
        std::string titleLower = metadata.title;
        std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
        if (titleLower.find(queryLower) != std::string::npos) {
            score += 10.0;
            hasExactMatch = true;
        }
        
        // Recherche dans l'auteur (exact match)
        if (!hasExactMatch || score < 10.0) {
            std::string authorLower = metadata.author;
            std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(), ::tolower);
            if (authorLower.find(queryLower) != std::string::npos) {
                score += 8.0;
                hasExactMatch = true;
            }
        }
        
        // Recherche dans le nom de fichier (exact match)
        if (!hasExactMatch || score < 8.0) {
            std::string filenameLower = metadata.filename;
            std::transform(filenameLower.begin(), filenameLower.end(), filenameLower.begin(), ::tolower);
            if (filenameLower.find(queryLower) != std::string::npos) {
                score += 5.0;
                hasExactMatch = true;
            }
        }
        
        if (hasExactMatch) {
            exactMatches++;
            scoredResults.push_back({&metadata, score});
        }
    }
    
    // Si on a déjà assez de résultats exacts, on s'arrête là
    if (scoredResults.size() >= maxResults) {
        std::sort(scoredResults.begin(), scoredResults.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (size_t i = 0; i < maxResults; ++i) {
            results.push_back(scoredResults[i].first);
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        if (totalTime > 50) {
            LOG_DEBUG("[SEARCH] query='{}': {} ms (exact matches: {}, returned: {})",
                      query, totalTime, exactMatches, results.size());
        }
        return results;
    }
    
    // Deuxième passe : fuzzy search seulement si nécessaire et si query >= 3 caractères
    if (useFuzzy && scoredResults.size() < maxResults) {
        // Créer un set des métadonnées déjà trouvées pour éviter les doublons
        std::unordered_set<const SidMetadata*> alreadyFound;
        for (const auto& pair : scoredResults) {
            alreadyFound.insert(pair.first);
        }
        
        // Parser la query en mots (pour recherche multi-mots)
        std::vector<std::string> queryWords;
        std::istringstream iss(queryLower);
        std::string word;
        while (iss >> word) {
            if (word.length() >= 2) { // Ignorer les mots trop courts
                queryWords.push_back(word);
            }
        }
        
        // Fuzzy search uniquement sur les éléments non trouvés
        for (const auto& metadata : m_metadata) {
            if (alreadyFound.find(&metadata) != alreadyFound.end()) {
                continue; // Déjà trouvé en exact match
            }
            
            double score = 0.0;
            
            // Préparer les chaînes de recherche
            std::string titleLower = metadata.title;
            std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
            std::string authorLower = metadata.author;
            std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(), ::tolower);
            std::string filenameLower = metadata.filename;
            std::transform(filenameLower.begin(), filenameLower.end(), filenameLower.begin(), ::tolower);
            
            // Recherche multi-mots : chaque mot de la query doit être trouvé quelque part
            if (queryWords.size() > 1) {
                // Vérifier que tous les mots sont présents (exact ou fuzzy)
                size_t wordsFound = 0;
                double totalFuzzyScore = 0.0;
                
                for (const auto& queryWord : queryWords) {
                    bool wordFound = false;
                    double bestWordScore = 0.0;
                    
                    // Chercher dans titre
                    if (titleLower.find(queryWord) != std::string::npos) {
                        wordFound = true;
                        bestWordScore = 1.0;
                    } else {
                        double fuzzyScore = fuzzyMatchFast(titleLower, queryWord);
                        if (fuzzyScore > 0.4) {
                            wordFound = true;
                            bestWordScore = std::max(bestWordScore, fuzzyScore);
                        }
                    }
                    
                    // Chercher dans auteur
                    if (!wordFound || bestWordScore < 1.0) {
                        if (authorLower.find(queryWord) != std::string::npos) {
                            wordFound = true;
                            bestWordScore = 1.0;
                        } else {
                            double fuzzyScore = fuzzyMatchFast(authorLower, queryWord);
                            if (fuzzyScore > 0.4) {
                                wordFound = true;
                                bestWordScore = std::max(bestWordScore, fuzzyScore * 0.8);
                            }
                        }
                    }
                    
                    // Chercher dans filename
                    if (!wordFound || bestWordScore < 1.0) {
                        if (filenameLower.find(queryWord) != std::string::npos) {
                            wordFound = true;
                            bestWordScore = 1.0;
                        } else {
                            double fuzzyScore = fuzzyMatchFast(filenameLower, queryWord);
                            if (fuzzyScore > 0.4) {
                                wordFound = true;
                                bestWordScore = std::max(bestWordScore, fuzzyScore * 0.6);
                            }
                        }
                    }
                    
                    if (wordFound) {
                        wordsFound++;
                        totalFuzzyScore += bestWordScore;
                    }
                }
                
                // Score basé sur le nombre de mots trouvés et la qualité des matches
                if (wordsFound == queryWords.size()) {
                    score = (totalFuzzyScore / queryWords.size()) * 5.0;
                } else if (wordsFound > 0) {
                    // Match partiel : pénalité
                    score = (static_cast<double>(wordsFound) / queryWords.size()) * (totalFuzzyScore / wordsFound) * 3.0;
                }
            } else {
                // Recherche simple (un seul mot) : comme avant
                double fuzzyScore = fuzzyMatchFast(titleLower, queryLower);
                if (fuzzyScore > 0.5) {
                    score += fuzzyScore * 5.0;
                }
                
                if (score == 0.0) {
                    fuzzyScore = fuzzyMatchFast(authorLower, queryLower);
                    if (fuzzyScore > 0.5) {
                        score += fuzzyScore * 4.0;
                    }
                }
                
                if (score == 0.0) {
                    fuzzyScore = fuzzyMatchFast(filenameLower, queryLower);
                    if (fuzzyScore > 0.5) {
                        score += fuzzyScore * 2.0;
                    }
                }
            }
            
            if (score > 0.0) {
                scoredResults.push_back({&metadata, score});
            }
        }
    }
    
    // Trier par score décroissant
    std::sort(scoredResults.begin(), scoredResults.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Retourner les résultats (limiter à maxResults)
    for (size_t i = 0; i < std::min(scoredResults.size(), maxResults); ++i) {
        results.push_back(scoredResults[i].first);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    // Log seulement si la recherche prend du temps (> 50ms)
    if (totalTime > 50) {
        LOG_DEBUG("[SEARCH] query='{}': {} ms (DB: {}, exact: {}, fuzzy: {}, results: {})",
                  query, totalTime, m_metadata.size(), exactMatches, 
                  useFuzzy ? "yes" : "no", results.size());
    }
    
    return results;
}

SidMetadata DatabaseManager::extractMetadata(const std::string& filepath) {
    try {
        SidTune tune(filepath.c_str());
        if (!tune.getStatus()) {
            return SidMetadata(); // Erreur
        }
        
        return SidMetadata::fromSidTune(filepath, &tune);
    } catch (const std::exception& e) {
        LOG_ERROR("Erreur lors de l'extraction des métadonnées de {}: {}", filepath, e.what());
        return SidMetadata();
    }
}

/**
 * Algorithme de fuzzy match optimisé (beaucoup plus rapide que Levenshtein complet)
 * 
 * NOTE IMPORTANTE: Ce n'est PAS une fonctionnalité de Glaze. Glaze est une bibliothèque de 
 * sérialisation JSON et ne fournit PAS de fonctionnalité de fuzzy search. Cette fonction est 
 * une implémentation personnalisée optimisée pour la recherche rapide dans une grande base de données.
 * 
 * Principe de fonctionnement :
 * 1. Vérifie d'abord si la query est une sous-chaîne exacte (match parfait = 1.0)
 * 2. Sinon, vérifie si tous les caractères de la query sont présents dans l'ordre
 *    (algorithme de "subsequence matching" optimisé)
 * 3. Calcule un score basé sur :
 *    - La proportion de caractères trouvés (60% du score)
 *    - La longueur de la séquence consécutive la plus longue (40% du score)
 * 
 * Complexité : O(n) où n = longueur de la chaîne à rechercher
 * (beaucoup plus rapide que Levenshtein qui est O(n*m))
 * 
 * Exemple :
 *   query = "david"
 *   str = "David Major - Cova"
 *   → Tous les caractères 'd','a','v','i','d' sont trouvés dans l'ordre
 *   → Score élevé (proche de 1.0)
 * 
 * @param str La chaîne dans laquelle chercher (ex: titre, auteur)
 * @param query La chaîne à rechercher (doit être en minuscules)
 * @return Score entre 0.0 (aucun match) et 1.0 (match parfait)
 */
double DatabaseManager::fuzzyMatchFast(const std::string& str, const std::string& query) {
    if (str.empty() || query.empty()) return 0.0;
    if (query.length() > str.length()) return 0.0;
    
    // Si la query est une sous-chaîne exacte, score parfait
    if (str.find(query) != std::string::npos) {
        return 1.0;
    }
    
    // Vérifier si tous les caractères de la query sont présents dans l'ordre (approximation rapide)
    size_t queryIdx = 0;
    size_t matches = 0;
    size_t consecutiveMatches = 0;
    size_t maxConsecutive = 0;
    
    for (size_t i = 0; i < str.length() && queryIdx < query.length(); ++i) {
        if (str[i] == query[queryIdx]) {
            matches++;
            consecutiveMatches++;
            maxConsecutive = std::max(maxConsecutive, consecutiveMatches);
            queryIdx++;
        } else {
            consecutiveMatches = 0;
        }
    }
    
    // Si tous les caractères de la query sont trouvés dans l'ordre
    if (queryIdx == query.length()) {
        // Score basé sur la proportion de caractères trouvés et la longueur de la séquence consécutive
        double matchRatio = static_cast<double>(matches) / query.length();
        double consecutiveRatio = static_cast<double>(maxConsecutive) / query.length();
        return (matchRatio * 0.6 + consecutiveRatio * 0.4);
    }
    
    // Aucun match
    return 0.0;
}

