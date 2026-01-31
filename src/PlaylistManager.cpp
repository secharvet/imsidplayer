#include "PlaylistManager.h"
#include "Config.h"
#include "DatabaseManager.h"
#include "Logger.h"
#include <algorithm>
#include <iostream>
#include <functional>
#include <chrono>
#include <unordered_map>

PlaylistManager::PlaylistManager()
    : m_root(std::make_unique<PlaylistNode>("Playlist", "", true)), m_currentNode(nullptr), m_shouldScrollToCurrent(false) {
}

void PlaylistManager::loadFromConfig(const Config& config) {
    // On ne charge plus la playlist depuis la config, elle est reconstruite depuis la DB
    // On restaure seulement le morceau en cours si possible
    if (!config.getCurrentFile().empty() && std::filesystem::exists(config.getCurrentFile())) {
        m_currentNode = findNodeByPath(config.getCurrentFile());
        if (m_currentNode) {
            m_shouldScrollToCurrent = true;
        }
    }
}

void PlaylistManager::rebuildFromDatabase(const DatabaseManager& db) {
    auto rebuildStart = std::chrono::high_resolution_clock::now();
    
    m_root->children.clear();
    m_currentNode = nullptr;
    
    // Étape 1: Récupérer toutes les métadonnées
    auto getAllMetaStart = std::chrono::high_resolution_clock::now();
    const auto& metadataList = db.getAllMetadata();
    auto getAllMetaEnd = std::chrono::high_resolution_clock::now();
    auto getAllMetaTime = std::chrono::duration_cast<std::chrono::milliseconds>(getAllMetaEnd - getAllMetaStart).count();
    LOG_INFO("[Playlist Rebuild] getAllMetadata: {} ms ({} entries)", getAllMetaTime, metadataList.size());
    
    if (metadataList.empty()) return;

    // Étape 2: Grouper les fichiers par rootFolder
    auto groupStart = std::chrono::high_resolution_clock::now();
    std::unordered_map<std::string, std::vector<const SidMetadata*>> filesByRoot;
    for (const auto& meta : metadataList) {
        std::string root = meta.rootFolder.empty() ? "" : meta.rootFolder;
        filesByRoot[root].push_back(&meta);
    }
    auto groupEnd = std::chrono::high_resolution_clock::now();
    auto groupTime = std::chrono::duration_cast<std::chrono::milliseconds>(groupEnd - groupStart).count();
    LOG_INFO("[Playlist Rebuild] Grouping by rootFolder: {} ms ({} root folders)", groupTime, filesByRoot.size());

    // Étape 3: Construire l'arborescence
    auto treeBuildStart = std::chrono::high_resolution_clock::now();
    size_t totalFilesAdded = 0;
    size_t totalFoldersCreated = 0;
    
    // Pour chaque rootFolder, créer un nœud racine et ajouter les fichiers
    for (const auto& [rootFolder, files] : filesByRoot) {
        auto rootStart = std::chrono::high_resolution_clock::now();
        PlaylistNode* rootNode = m_root.get();
        
        // Si rootFolder est spécifié, créer un nœud racine pour ce dossier
        if (!rootFolder.empty()) {
            // Vérifier si ce dossier racine existe déjà
            PlaylistNode* existingRoot = nullptr;
            for (auto& child : m_root->children) {
                if (child->isFolder && child->name == rootFolder) {
                    existingRoot = child.get();
                    break;
                }
            }
            
            if (!existingRoot) {
                auto newRoot = std::make_unique<PlaylistNode>(rootFolder, "", true);
                newRoot->parent = m_root.get();
                existingRoot = newRoot.get();
                m_root->children.push_back(std::move(newRoot));
            }
            rootNode = existingRoot;
        }
        
        // Ajouter les fichiers de ce rootFolder
        for (const auto* meta : files) {
            fs::path p(meta->filepath);
            std::vector<std::string> components;
            for (const auto& part : p) {
                if (!part.empty() && part != "/" && part != "\\") {
                    components.push_back(part.string());
                }
            }
            
            // Si rootFolder est spécifié, trouver où commence le chemin relatif
            // On cherche le rootFolder dans les composants (insensible à la casse)
            size_t startIdx = 0;
            if (!rootFolder.empty()) {
                bool found = false;
                for (size_t i = 0; i < components.size(); ++i) {
                    std::string compLower = components[i];
                    std::transform(compLower.begin(), compLower.end(), compLower.begin(), ::tolower);
                    std::string rootLower = rootFolder;
                    std::transform(rootLower.begin(), rootLower.end(), rootLower.begin(), ::tolower);
                    
                    if (compLower == rootLower) {
                        startIdx = i + 1;
                        found = true;
                        break;
                    }
                }
                
                // Si rootFolder non trouvé, utiliser une approche différente :
                // Pour tous les fichiers du même rootFolder, trouver le préfixe commun
                // et commencer après ce préfixe
                if (!found && files.size() > 1) {
                    // Trouver le préfixe commun de tous les chemins de ce rootFolder
                    std::string commonPrefix = meta->filepath;
                    size_t lastSlash = commonPrefix.find_last_of("/\\");
                    if (lastSlash != std::string::npos) {
                        commonPrefix = commonPrefix.substr(0, lastSlash + 1);
                    }
                    
                    for (const auto* otherMeta : files) {
                        if (otherMeta == meta) continue;
                        size_t j = 0;
                        while (j < commonPrefix.size() && j < otherMeta->filepath.size() && 
                               (std::tolower(commonPrefix[j]) == std::tolower(otherMeta->filepath[j]) || 
                                ((commonPrefix[j] == '/' || commonPrefix[j] == '\\') && 
                                 (otherMeta->filepath[j] == '/' || otherMeta->filepath[j] == '\\')))) {
                            j++;
                        }
                        commonPrefix = commonPrefix.substr(0, j);
                        size_t recalSlash = commonPrefix.find_last_of("/\\");
                        if (recalSlash != std::string::npos) {
                            commonPrefix = commonPrefix.substr(0, recalSlash + 1);
                        } else {
                            commonPrefix = "";
                            break;
                        }
                    }
                    
                    // Maintenant, trouver où commence commonPrefix dans le chemin actuel
                    if (!commonPrefix.empty()) {
                        fs::path commonPath(commonPrefix);
                        size_t commonComponents = 0;
                        for (const auto& part : commonPath) {
                            if (!part.empty() && part != "/" && part != "\\") {
                                commonComponents++;
                            }
                        }
                        startIdx = commonComponents;
                        found = true;
                    }
                }
                
                // Si toujours pas trouvé, chercher "C64Music" ou dossiers HVSC typiques
                if (!found) {
                    for (size_t i = 0; i < components.size(); ++i) {
                        std::string compLower = components[i];
                        std::transform(compLower.begin(), compLower.end(), compLower.begin(), ::tolower);
                        // Si on trouve "c64music" ou un dossier qui ressemble à une collection, commencer après
                        if (compLower == "c64music" || compLower.find("music") != std::string::npos) {
                            startIdx = i;
                            found = true;
                            break;
                        }
                    }
                }
                
                // Si toujours pas trouvé, commencer après les 3 premiers composants (home/user/path)
                // pour éviter d'afficher tout le chemin système
                if (!found && components.size() > 3) {
                    startIdx = 3;
                } else if (!found) {
                    // Dernier recours : commencer juste avant le nom de fichier
                    startIdx = components.size() > 1 ? components.size() - 1 : 0;
                }
            }
            
            PlaylistNode* current = rootNode;
            // Parcourir les dossiers (sauf le dernier qui est le fichier)
            // Si startIdx est au-delà de la taille, on ajoute juste le fichier
            if (startIdx < components.size() - 1) {
                for (size_t i = startIdx; i < components.size() - 1; ++i) {
                    const std::string& folderName = components[i];
                    PlaylistNode* foundFolder = nullptr;
                    for (auto& child : current->children) {
                        if (child->isFolder && child->name == folderName) {
                            foundFolder = child.get();
                            break;
                        }
                    }
                    if (!foundFolder) {
                        auto newFolder = std::make_unique<PlaylistNode>(folderName, "", true);
                        newFolder->parent = current;
                        foundFolder = newFolder.get();
                        current->children.push_back(std::move(newFolder));
                    }
                    current = foundFolder;
                }
            }

            std::string fileName = components.back();
            auto fileNode = std::make_unique<PlaylistNode>(fileName, meta->filepath, false);
            fileNode->parent = current;
            current->children.push_back(std::move(fileNode));
            totalFilesAdded++;
        }
        
        auto rootEnd = std::chrono::high_resolution_clock::now();
        auto rootTime = std::chrono::duration_cast<std::chrono::milliseconds>(rootEnd - rootStart).count();
        if (rootTime > 100) { // Log seulement si > 100ms
            LOG_INFO("[Playlist Rebuild] Root folder '{}': {} ms ({} files)", rootFolder.empty() ? "<empty>" : rootFolder, rootTime, files.size());
        }
    }
    
    auto treeBuildEnd = std::chrono::high_resolution_clock::now();
    auto treeBuildTime = std::chrono::duration_cast<std::chrono::milliseconds>(treeBuildEnd - treeBuildStart).count();
    LOG_INFO("[Playlist Rebuild] Tree construction: {} ms ({} files, {} folders)", treeBuildTime, totalFilesAdded, totalFoldersCreated);
    
    // Étape 4: Trier toute l'arborescence
    auto sortStart = std::chrono::high_resolution_clock::now();
    sortNode(m_root.get());
    auto sortEnd = std::chrono::high_resolution_clock::now();
    auto sortTime = std::chrono::duration_cast<std::chrono::milliseconds>(sortEnd - sortStart).count();
    LOG_INFO("[Playlist Rebuild] Sorting: {} ms", sortTime);
    
    // Étape 5: Restaurer le morceau courant après reconstruction
    auto restoreStart = std::chrono::high_resolution_clock::now();
    std::string currentFile = Config::getInstance().getCurrentFile();
    if (!currentFile.empty()) {
        m_currentNode = findNodeByPath(currentFile);
    }
    auto restoreEnd = std::chrono::high_resolution_clock::now();
    auto restoreTime = std::chrono::duration_cast<std::chrono::milliseconds>(restoreEnd - restoreStart).count();
    LOG_INFO("[Playlist Rebuild] Restore current file: {} ms", restoreTime);
    
    auto rebuildEnd = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(rebuildEnd - rebuildStart).count();
    LOG_INFO("[Playlist Rebuild] Total rebuild time: {} ms", totalTime);
}

void PlaylistManager::saveToConfig(Config& config) const {
    // Ne fait plus rien : la playlist n'est plus sauvegardée dans config.txt
}

void PlaylistManager::addFilePathToTree(const std::string& filepath) {
    fs::path p(filepath);
    if (!fs::exists(p)) return;

    // Décomposer le chemin en composants
    std::vector<std::string> components;
    for (const auto& part : p) {
        if (!part.empty() && part != "/" && part != "\\") {
            components.push_back(part.string());
        }
    }

    PlaylistNode* current = m_root.get();
    
    // Parcourir les dossiers parents
    for (size_t i = 0; i < components.size() - 1; ++i) {
        const std::string& folderName = components[i];
        
        // Chercher si le dossier existe déjà parmi les enfants
        PlaylistNode* foundFolder = nullptr;
        for (auto& child : current->children) {
            if (child->isFolder && child->name == folderName) {
                foundFolder = child.get();
                break;
            }
        }
        
        if (!foundFolder) {
            // Créer le dossier s'il n'existe pas
            auto newFolder = std::make_unique<PlaylistNode>(folderName, "", true);
            newFolder->parent = current;
            foundFolder = newFolder.get();
            current->children.push_back(std::move(newFolder));
        }
        
        current = foundFolder;
    }

    // Ajouter le fichier
    std::string fileName = components.back();
    auto fileNode = std::make_unique<PlaylistNode>(fileName, filepath, false);
    fileNode->parent = current;
    current->children.push_back(std::move(fileNode));
}

PlaylistNode* PlaylistManager::findNodeByPath(const std::string& filepath) {
    if (!m_root) return nullptr;

    // 1. On canonise la cible UNE SEULE FOIS (évite des milliers d'appels système)
    std::string target;
    try {
        target = fs::canonical(filepath).string();
    } catch (...) {
        // Si canonical échoue, utiliser le chemin tel quel
        target = filepath;
    }

    // 2. Lambda générique sans std::function (permet l'inlining par le compilateur)
    auto findRecursive = [&](auto& self, PlaylistNode* node) -> PlaylistNode* {
        if (!node) return nullptr;
        
        // Comparaison de string simple (très rapide, pas d'appels système)
        if (!node->isFolder && !node->filepath.empty()) {
            // Comparer directement les strings (les filepath sont déjà en format absolu dans le cache)
            if (node->filepath == target) {
                return node;
            }
        }
        
        for (auto& child : node->children) {
            PlaylistNode* found = self(self, child.get());
            if (found) return found;
        }
        
        return nullptr;
    };
    
    return findRecursive(findRecursive, m_root.get());
}

void PlaylistManager::addFile(const fs::path& path) {
    // Note: l'ajout manuel via drag & drop devrait idéalement passer par la DB
    // pour que ce soit permanent. Pour l'instant on l'ajoute à l'arbre.
    addFilePathToTree(path.string());
    sortNode(m_root.get());
}

void PlaylistManager::addDirectory(const fs::path& path) {
    // Créer un nœud racine avec le nom du dossier déposé
    std::string rootName = path.filename().string();
    if (rootName.empty()) {
        rootName = path.string(); // Fallback si filename() est vide
    }
    
    // Vérifier si ce dossier existe déjà à la racine
    PlaylistNode* existingRoot = nullptr;
    for (auto& child : m_root->children) {
        if (child->isFolder && child->name == rootName) {
            existingRoot = child.get();
            break;
        }
    }
    
    if (!existingRoot) {
        // Créer le dossier racine
        auto rootNode = std::make_unique<PlaylistNode>(rootName, "", true);
        rootNode->parent = m_root.get();
        existingRoot = rootNode.get();
        m_root->children.push_back(std::move(rootNode));
    }
    
    // Ajouter récursivement le contenu du dossier dans ce nœud racine
    addDirectoryRecursive(path, existingRoot);
    sortNode(m_root.get());
}

std::vector<PlaylistNode*> PlaylistManager::getAllFiles() const {
    std::vector<PlaylistNode*> files;
    std::function<void(PlaylistNode*)> collect;
    collect = [&](PlaylistNode* node) {
        if (!node) return;
        if (!node->isFolder && !node->filepath.empty()) {
            files.push_back(node);
        }
        for (auto& child : node->children) {
            collect(child.get());
        }
    };
    collect(m_root.get());
    return files;
}

PlaylistNode* PlaylistManager::getPreviousFile() {
    auto allFiles = getAllFiles();
    if (allFiles.empty() || !m_currentNode) return nullptr;
    
    auto it = std::find(allFiles.begin(), allFiles.end(), m_currentNode);
    if (it != allFiles.end()) {
        if (it == allFiles.begin()) {
            it = allFiles.end() - 1;
        } else {
            --it;
        }
        return *it;
    }
    return allFiles.empty() ? nullptr : allFiles.back();
}

PlaylistNode* PlaylistManager::getNextFile() {
    auto allFiles = getAllFiles();
    if (allFiles.empty() || !m_currentNode) return nullptr;
    
    auto it = std::find(allFiles.begin(), allFiles.end(), m_currentNode);
    if (it != allFiles.end()) {
        ++it;
        if (it == allFiles.end()) {
            it = allFiles.begin();
        }
        return *it;
    }
    return allFiles.empty() ? nullptr : allFiles[0];
}

void PlaylistManager::clear() {
    m_root->children.clear();
    m_currentNode = nullptr;
}

void PlaylistManager::sortNode(PlaylistNode* node) {
    std::sort(node->children.begin(), node->children.end(),
        [](const std::unique_ptr<PlaylistNode>& a, const std::unique_ptr<PlaylistNode>& b) {
            if (a->isFolder != b->isFolder) {
                return a->isFolder > b->isFolder;
            }
            return a->name < b->name;
        });
    for (auto& child : node->children) {
        if (child->isFolder) {
            sortNode(child.get());
        }
    }
}

void PlaylistManager::addDirectoryRecursive(const fs::path& dir, PlaylistNode* parent) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_directory()) {
                auto folderNode = std::make_unique<PlaylistNode>(
                    entry.path().filename().string(), "", true);
                folderNode->parent = parent;
                PlaylistNode* folderPtr = folderNode.get();
                parent->children.push_back(std::move(folderNode));
                addDirectoryRecursive(entry.path(), folderPtr);
            } else if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".sid") {
                    auto fileNode = std::make_unique<PlaylistNode>(
                        entry.path().filename().string(), 
                        entry.path().string(), 
                        false);
                    fileNode->parent = parent;
                    parent->children.push_back(std::move(fileNode));
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Erreur lecture dossier: " << e.what() << std::endl;
    }
}

std::unique_ptr<PlaylistNode> PlaylistManager::createFilteredTree(
    PlaylistNode* sourceNode,
    std::function<bool(PlaylistNode*)> filterFunc) const {
    if (!sourceNode) return nullptr;
    
    if (!sourceNode->isFolder) {
        if (filterFunc(sourceNode)) {
            auto filteredNode = std::make_unique<PlaylistNode>(
                sourceNode->name, 
                sourceNode->filepath, 
                false);
            filteredNode->parent = nullptr; 
            return filteredNode;
        }
        return nullptr;
    }
    
    std::unique_ptr<PlaylistNode> filteredFolder = nullptr;
    std::vector<std::unique_ptr<PlaylistNode>> filteredChildren;
    
    for (auto& child : sourceNode->children) {
        auto filteredChild = createFilteredTree(child.get(), filterFunc);
        if (filteredChild) {
            filteredChildren.push_back(std::move(filteredChild));
        }
    }
    
    if (!filteredChildren.empty()) {
        filteredFolder = std::make_unique<PlaylistNode>(
            sourceNode->name,
            "",
            true);
        filteredFolder->children = std::move(filteredChildren);
        
        for (auto& child : filteredFolder->children) {
            child->parent = filteredFolder.get();
        }
    }
    
    return filteredFolder;
}

PlaylistNode* PlaylistManager::findFirstMatchingNode(
    PlaylistNode* node,
    std::function<bool(PlaylistNode*)> filterFunc) const {
    if (!node) return nullptr;
    
    if (!node->isFolder) {
        if (filterFunc(node)) {
            return node;
        }
        return nullptr;
    }
    
    for (auto& child : node->children) {
        PlaylistNode* found = findFirstMatchingNode(child.get(), filterFunc);
        if (found) {
            return found;
        }
    }
    
    return nullptr;
}
