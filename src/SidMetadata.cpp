#include "SidMetadata.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <chrono>

SidMetadata SidMetadata::fromSidTune(const std::string& filepath, SidTune* tune) {
    SidMetadata metadata;
    metadata.filepath = filepath;
    
    fs::path path(filepath);
    metadata.filename = path.filename().string();
    
    // Informations sur le fichier
    try {
        if (fs::exists(path)) {
            metadata.fileSize = fs::file_size(path);
            auto ftime = fs::last_write_time(path);
            metadata.lastModified = std::chrono::duration_cast<std::chrono::seconds>(
                ftime.time_since_epoch()).count();
        }
    } catch (...) {
        // Ignorer les erreurs
    }
    
    if (!tune) {
        return metadata;
    }
    
    const SidTuneInfo* info = tune->getInfo();
    if (!info) {
        return metadata;
    }
    
    // Informations de base
    metadata.numberOfSongs = info->songs();
    metadata.defaultSong = info->startSong();
    
    // Modèle SID
    if (info->sidModel(0) == SidTuneInfo::SIDMODEL_8580) {
        metadata.sidModel = "8580";
    } else {
        metadata.sidModel = "6581";
    }
    
    // Vitesse d'horloge
    if (info->clockSpeed() == SidTuneInfo::CLOCK_PAL) {
        metadata.clockSpeed = 1; // PAL
    } else if (info->clockSpeed() == SidTuneInfo::CLOCK_NTSC) {
        metadata.clockSpeed = 2; // NTSC
    } else {
        metadata.clockSpeed = 0; // Unknown
    }
    
    // Titre, auteur, date
    if (info->numberOfInfoStrings() > 0) {
        const char* title = info->infoString(0);
        if (title && strlen(title) > 0) {
            metadata.title = title;
        }
    }
    
    if (info->numberOfInfoStrings() > 1) {
        const char* author = info->infoString(1);
        if (author && strlen(author) > 0) {
            metadata.author = author;
        }
    }
    
    if (info->numberOfInfoStrings() > 2) {
        const char* released = info->infoString(2);
        if (released && strlen(released) > 0) {
            metadata.released = released;
        }
    }
    
    // Toutes les chaînes d'information
    for (unsigned int i = 0; i < info->numberOfInfoStrings(); ++i) {
        const char* infoStr = info->infoString(i);
        if (infoStr && strlen(infoStr) > 0) {
            metadata.infoStrings.push_back(infoStr);
        }
    }
    
    // Générer le hash basé sur les métadonnées (rapide, 32-bit)
    metadata.metadataHash = generateMetadataHash(
        metadata.title, 
        metadata.author, 
        metadata.released, 
        metadata.sidModel, 
        metadata.clockSpeed
    );
    
    return metadata;
}

bool SidMetadata::isFileChanged() const {
    if (filepath.empty() || !fs::exists(filepath)) {
        return true; // Fichier n'existe plus ou chemin invalide
    }
    
    try {
        auto ftime = fs::last_write_time(filepath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        int64_t currentModified = std::chrono::duration_cast<std::chrono::seconds>(
            sctp.time_since_epoch()).count();
        
        if (currentModified != lastModified) {
            return true; // Fichier modifié
        }
        
        int64_t currentSize = fs::file_size(filepath);
        if (currentSize != fileSize) {
            return true; // Taille différente
        }
        
        // Note: On ne vérifie plus le hash du fichier, seulement taille et date
    } catch (...) {
        return true; // Erreur = considérer comme changé
    }
    
    return false;
}

uint32_t SidMetadata::generateMetadataHash(const std::string& title, const std::string& author, 
                                            const std::string& released, const std::string& sidModel, 
                                            int clockSpeed) {
    // Hash FNV-1a 32-bit (rapide et efficace)
    uint32_t hash = 2166136261u; // FNV offset basis
    
    auto hashString = [&hash](const std::string& str) {
        for (char c : str) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u; // FNV prime
        }
    };
    
    // Combiner toutes les métadonnées
    hashString(title);
    hashString(author);
    hashString(released);
    hashString(sidModel);
    
    // Ajouter clockSpeed
    hash ^= static_cast<uint32_t>(clockSpeed);
    hash *= 16777619u;
    
    return hash;
}

