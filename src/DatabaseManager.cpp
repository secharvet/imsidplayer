#include "DatabaseManager.h"
#include "Utils.h"
#include "PlaylistManager.h"
#include "Logger.h"
#include "SongLengthDB.h"
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <chrono>
#include <glaze/glaze.hpp>

namespace fs = std::filesystem;

DatabaseManager::DatabaseManager() : m_cacheValid(false) {
    fs::path configDir = getConfigDir();
    m_databasePath = (configDir / "database.json").string();
}

bool DatabaseManager::load() {
    auto loadStart = std::chrono::high_resolution_clock::now();
    
    if (!fs::exists(m_databasePath)) {
        LOG_INFO("Database does not exist yet, creating a new database");
        m_rootFolders.clear();
        m_cacheValid = false;
        return true; // Pas d'erreur, juste pas de fichier existant
    }
    
    try {
        // Mode Turbo: Lecture + Parsing JSON en une seule opération optimisée
        auto readParseStart = std::chrono::high_resolution_clock::now();
        
        // Obtenir la taille du fichier pour le log
        size_t fileSize = 0;
        if (fs::exists(m_databasePath)) {
            fileSize = fs::file_size(m_databasePath);
        }
        
        std::vector<RootFolderEntry> newStructure;
        auto error = glz::read_file_json(newStructure, m_databasePath, std::string{});
    
        auto readParseEnd = std::chrono::high_resolution_clock::now();
        auto readParseTime = std::chrono::duration_cast<std::chrono::milliseconds>(readParseEnd - readParseStart).count();
        
        if (error) {
            LOG_ERROR("Error reading database file: error code {}", static_cast<int>(error.ec));
            return false;
        }
        
        LOG_INFO("[DB Load] File read + JSON parsing (Turbo mode): {} ms ({} bytes)", readParseTime, fileSize);
        
        // Copie des données
        auto copyStart = std::chrono::high_resolution_clock::now();
        m_rootFolders = newStructure;
        auto copyEnd = std::chrono::high_resolution_clock::now();
        auto copyTime = std::chrono::duration_cast<std::chrono::milliseconds>(copyEnd - copyStart).count();
        LOG_INFO("[DB Load] Data copy: {} ms", copyTime);
        
        // Étape 4: Reconstruction des chemins absolus
        auto pathStart = std::chrono::high_resolution_clock::now();
        for (auto& rootEntry : m_rootFolders) {
            if (rootEntry.rootPath.empty()) {
                // Si rootPath est vide, essayer de le déduire depuis le premier filepath
                if (!rootEntry.sidList.empty() && !rootEntry.sidList[0].filepath.empty()) {
                    fs::path firstFile(rootEntry.sidList[0].filepath);
                    if (firstFile.is_absolute()) {
                        // C'est encore un chemin absolu (ancien format), extraire rootPath
                        rootEntry.rootPath = firstFile.parent_path().string();
                        // Garder le filepath tel quel pour l'instant
                    }
                }
            }
        }
        auto pathEnd = std::chrono::high_resolution_clock::now();
        auto pathTime = std::chrono::duration_cast<std::chrono::milliseconds>(pathEnd - pathStart).count();
        LOG_INFO("[DB Load] Path reconstruction: {} ms", pathTime);
        
        // Étape 5: Reconstruire le cache et les index depuis la base de données
        rebuildCacheAndIndexes();
        
        size_t totalFiles = 0;
        for (const auto& root : m_rootFolders) {
            totalFiles += root.sidList.size();
        }
        
        auto loadEnd = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(loadEnd - loadStart).count();
        
        LOG_INFO("[DB Load] Total load time: {} ms ({} root folders, {} total files, {} filepath entries, {} hash entries)", 
                 totalTime, m_rootFolders.size(), totalFiles, m_filepathIndex.size(), m_hashIndex.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading database: {}", e.what());
        return false;
    }
}

bool DatabaseManager::save() {
    try {
        // Préparer la structure hiérarchique avec chemins relatifs
        std::vector<RootFolderEntry> saveStructure = m_rootFolders;
        
        // Convertir les chemins absolus en relatifs pour chaque entrée
        for (auto& rootEntry : saveStructure) {
            if (rootEntry.rootPath.empty()) {
                continue; // Pas de rootPath, garder les filepath tels quels
            }
            
            fs::path rootPath(rootEntry.rootPath);
            for (auto& meta : rootEntry.sidList) {
                // Si le filepath est absolu, le convertir en relatif
                if (!meta.filepath.empty()) {
                    fs::path filePath(meta.filepath);
                    if (filePath.is_absolute()) {
                        try {
                            meta.filepath = fs::relative(filePath, rootPath).string();
                        } catch (...) {
                            // Si relative échoue, garder le chemin absolu
                        }
                    }
                }
                // Supprimer rootFolder de chaque entrée (déjà dans RootFolderEntry)
                meta.rootFolder = "";
            }
        }
        
        // Utiliser prettify pour formater le JSON avec retours à la ligne (lisible)
        std::string jsonStr;
        auto error = glz::write<glz::opts{.prettify = true}>(saveStructure, jsonStr);
        if (error) {
            LOG_ERROR("Error writing database: {}", glz::format_error(error, jsonStr));
            return false;
        }
        
        std::ofstream file(m_databasePath);
        if (!file) {
            LOG_ERROR("Failed to write to database");
            return false;
        }
        
        file << jsonStr;
        
        size_t totalFiles = 0;
        for (const auto& root : saveStructure) {
            totalFiles += root.sidList.size();
        }
        
        LOG_INFO("Database saved: {} root folders, {} total files", 
                 saveStructure.size(), totalFiles);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error saving database: {}", e.what());
        return false;
    }
}

void DatabaseManager::rebuildCacheAndIndexes() const {
    auto start = std::chrono::high_resolution_clock::now();
    
    m_metadataCache.clear();
    m_filepathIndex.clear();
    m_hashIndex.clear();
    
    // Parcourir tous les RootFolderEntry et reconstruire les chemins absolus
    for (const auto& rootEntry : m_rootFolders) {
        fs::path rootPath(rootEntry.rootPath);
        
        for (const auto& meta : rootEntry.sidList) {
            // Reconstruire le chemin absolu
            SidMetadata fullMeta = meta;
            if (!rootPath.empty() && !meta.filepath.empty()) {
                fs::path filePath(meta.filepath);
                if (filePath.is_relative()) {
                    // Chemin relatif : le combiner avec rootPath
                    fullMeta.filepath = (rootPath / filePath).string();
                } else {
                    // Déjà absolu, garder tel quel
                    fullMeta.filepath = meta.filepath;
                }
            }
            // Restaurer rootFolder pour compatibilité
            fullMeta.rootFolder = rootEntry.rootFolder;
            
            size_t index = m_metadataCache.size();
            m_metadataCache.push_back(fullMeta);
            
            // Indexer par filepath (absolu)
            if (!fullMeta.filepath.empty()) {
                m_filepathIndex[fullMeta.filepath] = index;
            }
            
            // Indexer par metadataHash
            if (fullMeta.metadataHash != 0) {
                m_hashIndex[fullMeta.metadataHash] = index;
            }
        }
    }
    
    m_cacheValid = true;
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    LOG_INFO("rebuildCacheAndIndexes completed in {} ms ({} entries, {} filepath index, {} hash index)", 
             duration.count(), m_metadataCache.size(), m_filepathIndex.size(), m_hashIndex.size());
}

bool DatabaseManager::clear() {
    // Vider la base de données en mémoire
    m_rootFolders.clear();
    m_metadataCache.clear();
    m_filepathIndex.clear();
    m_hashIndex.clear();
    m_cacheValid = false;
    
    // Supprimer le fichier sur disque
    if (fs::exists(m_databasePath)) {
        try {
            fs::remove(m_databasePath);
            LOG_INFO("Database file deleted: {}", m_databasePath);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to delete database file {}: {}", m_databasePath, e.what());
            return false;
        }
    }
    
    LOG_INFO("Database cleared (file did not exist)");
    return true;
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
        
        // Déterminer le rootFolder : remonter jusqu'au premier enfant direct de m_root
        // (c'est le dossier déposé par drag & drop)
        std::string rootFolder = "";
        PlaylistNode* current = node->parent;
        while (current && current->parent) {
            // Si le parent de current est m_root (parent == nullptr), alors current est le rootFolder
            if (current->parent->parent == nullptr) {
                // current->parent est m_root, donc current est le rootFolder
                if (current->isFolder) {
                    rootFolder = current->name;
                }
                break;
            }
            current = current->parent;
        }
        
        // Si on n'a pas trouvé de rootFolder, essayer une approche alternative :
        // extraire le rootFolder depuis le filepath en cherchant le nom du dossier déposé
        if (rootFolder.empty() && !node->filepath.empty()) {
            // Chercher dans les enfants directs de m_root pour trouver lequel correspond au chemin
            // Cette approche est un fallback si la remontée dans l'arbre a échoué
            fs::path filePath(node->filepath);
            for (const auto& part : filePath.parent_path()) {
                // Vérifier si ce composant correspond à un enfant direct de m_root
                // (cette logique nécessiterait d'avoir accès à m_root, ce qui n'est pas le cas ici)
                // Pour l'instant, on laisse rootFolder vide et on le déterminera autrement
            }
        }
        
        if (indexFile(node->filepath, rootFolder)) {
            indexed++;
        }
    }
    
    save();
    return indexed;
}

bool DatabaseManager::indexFile(const std::string& filepath, const std::string& rootFolder) {
    if (!fs::exists(filepath)) {
        return false;
    }
    
    // OPTIMISATION : Utiliser les index pour des recherches O(1) au lieu de O(n)
    // Maintenir les index à jour pendant l'indexation
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    
    // Recherche O(1) par filepath
    auto filepathIt = m_filepathIndex.find(filepath);
    if (filepathIt != m_filepathIndex.end()) {
        // Fichier déjà indexé, trouver dans quelle RootFolderEntry il se trouve
        const SidMetadata& cachedMeta = m_metadataCache[filepathIt->second];
        std::string existingRootFolder = cachedMeta.rootFolder;
        
        // Trouver la RootFolderEntry correspondante
        for (auto& rootEntry : m_rootFolders) {
            if (rootEntry.rootFolder == existingRootFolder) {
                // Trouver le fichier dans sidList (on connaît déjà l'index)
                size_t cacheIndex = filepathIt->second;
                // Reconstruire le chemin relatif depuis le cache
                fs::path rootPath(rootEntry.rootPath);
                fs::path cachedPath(cachedMeta.filepath);
                
                // Trouver l'entrée correspondante dans sidList
                for (auto& meta : rootEntry.sidList) {
                    fs::path metaPath = meta.filepath.empty() ? fs::path() :
                                       (meta.filepath[0] == '/' ? fs::path(meta.filepath) : rootPath / meta.filepath);
                    
                    if (metaPath.string() == filepath) {
                        // Fichier trouvé, vérifier s'il a changé
                        if (!meta.isFileChanged()) {
                            // Fichier à jour, mettre à jour rootFolder si fourni
                            if (!rootFolder.empty() && rootEntry.rootFolder.empty()) {
                                rootEntry.rootFolder = rootFolder;
                            }
                            return false;
                        }
                        
                        // Fichier a changé : réindexer et recalculer le MD5
                        meta = extractMetadata(filepath);
                        if (meta.filepath.empty()) {
                            return false;
                        }
                        
                        // Convertir en chemin relatif
                        if (!rootEntry.rootPath.empty()) {
                            try {
                                fs::path absPath(meta.filepath);
                                fs::path root(rootEntry.rootPath);
                                meta.filepath = fs::relative(absPath, root).string();
                            } catch (...) {
                                // Si relative échoue, garder le chemin absolu
                            }
                        }
                        
                        // Mettre à jour rootFolder si fourni
                        if (!rootFolder.empty() && rootEntry.rootFolder.empty()) {
                            rootEntry.rootFolder = rootFolder;
                        }
                        
                        // Recalculer le MD5 car le fichier a changé
                        meta.md5Hash = calculateFileMD5(filepath);
                        populateSongLengths(meta);
                        meta.rootFolder = ""; // Plus besoin dans chaque entrée
                        
                        // Mettre à jour le cache et les index
                        m_metadataCache[cacheIndex] = cachedMeta; // Mettre à jour avec nouvelles données
                        m_metadataCache[cacheIndex].filepath = filepath;
                        m_metadataCache[cacheIndex].rootFolder = rootEntry.rootFolder;
                        return true; // Fichier réindexé
                    }
                }
                break;
            }
        }
    }
    
    // Extraire les métadonnées pour obtenir le metadataHash
    SidMetadata metadata = extractMetadata(filepath);
    if (metadata.filepath.empty() || metadata.metadataHash == 0) {
        return false; // Erreur lors de l'extraction
    }
    
    // Déterminer le rootPath depuis le filepath
    fs::path filePath(filepath);
    std::string rootPathStr = "";
    if (!rootFolder.empty()) {
        // Chercher le rootFolder dans le chemin
        fs::path current = filePath;
        while (current.has_parent_path() && current.parent_path() != current) {
            if (current.filename().string() == rootFolder) {
                rootPathStr = current.string();
                break;
            }
            current = current.parent_path();
        }
    }
    // Si rootPath non trouvé, utiliser le parent du fichier
    if (rootPathStr.empty()) {
        rootPathStr = filePath.parent_path().string();
    }
    
    // Trouver ou créer la RootFolderEntry correspondante
    size_t rootIndex = SIZE_MAX;
    for (size_t i = 0; i < m_rootFolders.size(); ++i) {
        if (m_rootFolders[i].rootFolder == rootFolder) {
            rootIndex = i;
            break;
        }
    }
    
    if (rootIndex == SIZE_MAX) {
        // Créer une nouvelle RootFolderEntry
        RootFolderEntry newRoot;
        newRoot.rootPath = rootPathStr;
        newRoot.rootFolder = rootFolder;
        rootIndex = m_rootFolders.size();
        m_rootFolders.push_back(newRoot);
    }
    
    RootFolderEntry& rootEntry = m_rootFolders[rootIndex];
    
    // OPTIMISATION : Recherche O(1) par metadataHash au lieu de O(n)
    auto hashIt = m_hashIndex.find(metadata.metadataHash);
    if (hashIt != m_hashIndex.end()) {
        // Même morceau trouvé (O(1) !)
        const SidMetadata& existingCached = m_metadataCache[hashIt->second];
        std::string existingRootFolder = existingCached.rootFolder;
        
        // Si le rootFolder est différent (ou l'un est vide et l'autre non), créer une nouvelle entrée (dupliquer)
        bool shouldDuplicate = false;
        if (!rootFolder.empty() && !existingRootFolder.empty()) {
            shouldDuplicate = (existingRootFolder != rootFolder);
        } else if (!rootFolder.empty() && existingRootFolder.empty()) {
            shouldDuplicate = true;
        } else if (rootFolder.empty() && !existingRootFolder.empty()) {
            shouldDuplicate = true;
        }
        
        if (shouldDuplicate) {
            // Créer une nouvelle entrée pour ce rootFolder différent
            metadata.md5Hash = calculateFileMD5(filepath);
            populateSongLengths(metadata);
            // Convertir en chemin relatif
            SidMetadata relMeta = metadata;
            if (!rootEntry.rootPath.empty()) {
                try {
                    fs::path absPath(relMeta.filepath);
                    fs::path root(rootEntry.rootPath);
                    relMeta.filepath = fs::relative(absPath, root).string();
                } catch (...) {
                    // Si relative échoue, garder le chemin absolu
                }
            }
            relMeta.rootFolder = ""; // Plus besoin dans chaque entrée
            rootEntry.sidList.push_back(relMeta);
            
            // Mettre à jour les index
            size_t newIndex = m_metadataCache.size();
            SidMetadata fullMeta = relMeta;
            fullMeta.filepath = filepath; // Chemin absolu pour le cache
            fullMeta.rootFolder = rootEntry.rootFolder;
            m_metadataCache.push_back(fullMeta);
            m_filepathIndex[filepath] = newIndex;
            // Ne pas mettre à jour m_hashIndex car on veut garder la première occurrence comme référence
            
            return true;
        }
        
        // Même rootFolder : trouver dans la RootFolderEntry et mettre à jour
        if (existingRootFolder == rootFolder) {
            // Trouver l'entrée dans sidList
            for (auto& existingMeta : rootEntry.sidList) {
                if (existingMeta.metadataHash == metadata.metadataHash) {
                    // Mettre à jour le filepath si différent
                    fs::path existingPath;
                    if (existingMeta.filepath.empty()) {
                        existingPath = fs::path();
                    } else if (!existingMeta.filepath.empty() && existingMeta.filepath[0] == '/') {
                        existingPath = fs::path(existingMeta.filepath);
                    } else if (!rootEntry.rootPath.empty()) {
                        existingPath = fs::path(rootEntry.rootPath) / existingMeta.filepath;
                    }
                    
                    if (existingPath.string() != filepath) {
                        // Convertir le nouveau filepath en relatif
                        if (!rootEntry.rootPath.empty()) {
                            try {
                                fs::path absPath(filepath);
                                fs::path root(rootEntry.rootPath);
                                existingMeta.filepath = fs::relative(absPath, root).string();
                            } catch (...) {
                                existingMeta.filepath = filepath; // Garder absolu si relative échoue
                            }
                        } else {
                            existingMeta.filepath = filepath;
                        }
                    }
                    
                    // Vérifier si le fichier a changé
                    if (existingMeta.isFileChanged()) {
                        existingMeta = extractMetadata(filepath);
                        if (existingMeta.filepath.empty()) {
                            return false;
                        }
                        // Convertir en relatif
                        if (!rootEntry.rootPath.empty()) {
                            try {
                                fs::path absPath(existingMeta.filepath);
                                fs::path root(rootEntry.rootPath);
                                existingMeta.filepath = fs::relative(absPath, root).string();
                            } catch (...) {
                                // Garder absolu si relative échoue
                            }
                        }
                        existingMeta.md5Hash = calculateFileMD5(filepath);
                        populateSongLengths(existingMeta);
                    } else if (existingMeta.md5Hash.empty()) {
                        existingMeta.md5Hash = calculateFileMD5(filepath);
                        populateSongLengths(existingMeta);
                    }
                    
                    existingMeta.rootFolder = ""; // Plus besoin
                    
                    // Mettre à jour le cache
                    m_metadataCache[hashIt->second].filepath = filepath;
                    m_filepathIndex[filepath] = hashIt->second;
                    
                    return true;
                }
            }
        }
        
        // Même morceau mais rootFolder différent : déjà géré par shouldDuplicate
        return false;
    }
    
    // Nouveau morceau : ajouter les métadonnées et calculer le MD5
    metadata.md5Hash = calculateFileMD5(filepath);
    populateSongLengths(metadata);
    
    // Convertir en chemin relatif
    SidMetadata relMeta = metadata;
    if (!rootEntry.rootPath.empty()) {
        try {
            fs::path absPath(relMeta.filepath);
            fs::path root(rootEntry.rootPath);
            relMeta.filepath = fs::relative(absPath, root).string();
        } catch (...) {
            // Si relative échoue, garder le chemin absolu
        }
    }
    relMeta.rootFolder = ""; // Plus besoin dans chaque entrée
    rootEntry.sidList.push_back(relMeta);
    
    // Mettre à jour les index en temps réel (O(1))
    size_t newIndex = m_metadataCache.size();
    SidMetadata fullMeta = relMeta;
    fullMeta.filepath = filepath; // Chemin absolu pour le cache
    fullMeta.rootFolder = rootEntry.rootFolder;
    m_metadataCache.push_back(fullMeta);
    m_filepathIndex[filepath] = newIndex;
    m_hashIndex[metadata.metadataHash] = newIndex;
    
    return true;
}

bool DatabaseManager::isIndexed(const std::string& filepath) const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    auto it = m_filepathIndex.find(filepath);
    if (it != m_filepathIndex.end()) {
        // Vérifier si le fichier a changé
        const SidMetadata& metadata = m_metadataCache[it->second];
        return !metadata.isFileChanged();
    }
    return false;
}

bool DatabaseManager::isIndexedByMetadataHash(uint32_t metadataHash) const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    auto it = m_hashIndex.find(metadataHash);
    return it != m_hashIndex.end();
}

const SidMetadata* DatabaseManager::getMetadata(const std::string& filepath) const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    auto it = m_filepathIndex.find(filepath);
    if (it != m_filepathIndex.end()) {
        return &m_metadataCache[it->second];
    }
    return nullptr;
}

const SidMetadata* DatabaseManager::getMetadataByHash(uint32_t metadataHash) const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    auto it = m_hashIndex.find(metadataHash);
    if (it == m_hashIndex.end()) {
        return nullptr;
    }
    return &m_metadataCache[it->second];
}

std::vector<SidMetadata> DatabaseManager::getAllMetadata() const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    return m_metadataCache;
}

size_t DatabaseManager::getCount() const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
    return m_metadataCache.size();
}

std::unordered_set<uint32_t> DatabaseManager::getIndexedMetadataHashes() const {
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
    }
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
    
    if (!m_cacheValid) {
        rebuildCacheAndIndexes();
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
    for (const auto& metadata : m_metadataCache) {
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
        for (const auto& metadata : m_metadataCache) {
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
                scoredResults.push_back(std::make_pair(&metadata, score));
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
                  query, totalTime, m_metadataCache.size(), exactMatches, 
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
        
        SidMetadata metadata = SidMetadata::fromSidTune(filepath, &tune);
        
        // Le MD5 sera calculé dans indexFile() si nécessaire
        // (évite de recalculer si déjà présent dans la base)
        
        return metadata;
    } catch (const std::exception& e) {
        LOG_ERROR("Error extracting metadata from {}: {}", filepath, e.what());
        return SidMetadata();
    }
}

void DatabaseManager::populateSongLengths(SidMetadata& metadata) const {
    // Récupérer les songlengths depuis SongLengthDB si disponible
    if (!metadata.md5Hash.empty()) {
        SongLengthDB& songLengthDB = SongLengthDB::getInstance();
        if (songLengthDB.isLoaded()) {
            metadata.songLengths = songLengthDB.getDurations(metadata.md5Hash);
        }
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
 * 1. Recherche exacte rapide (O(n)) : vérifie si la query est contenue dans title/author
 * 2. Si pas de résultats exacts et query >= 3 caractères : fuzzy match (O(n*m))
 * 3. Scoring : exact match = 100, fuzzy match = score de similarité (0-100)
 * 4. Tri par score décroissant et limitation à 25 résultats
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
