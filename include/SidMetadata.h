#ifndef SID_METADATA_H
#define SID_METADATA_H

#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <glaze/glaze.hpp>

namespace fs = std::filesystem;

// Structure pour stocker les métadonnées d'un fichier SID
struct SidMetadata {
    std::string filepath;           // Chemin complet du fichier
    std::string filename;           // Nom du fichier
    std::string title;              // Titre de la chanson
    std::string author;             // Auteur/compositeur
    std::string released;           // Date de sortie
    std::string sidModel;           // Modèle SID (6581 ou 8580)
    int numberOfSongs;              // Nombre de sous-chansons
    int defaultSong;                // Chanson par défaut
    int clockSpeed;                 // Vitesse d'horloge (PAL/NTSC)
    std::vector<std::string> infoStrings; // Autres informations
    uint32_t metadataHash;          // Hash 32-bit basé sur métadonnées (title+author+released+sidModel+clockSpeed)
    std::string md5Hash;            // Hash MD5 du fichier (pour matching avec Songlengths.md5)
    int64_t fileSize;               // Taille du fichier
    int64_t lastModified;           // Date de modification (timestamp)
    uint16_t hvscNum;               // Numéro de version HVSC (ex: 84)
    std::string rootFolder;         // Dossier racine (nom du dossier déposé, ex: "HVSCallofthem24")
    std::vector<double> songLengths; // Durées des subsongs en secondes (depuis Songlengths.md5)
    
    // Constructeur par défaut
    SidMetadata() : numberOfSongs(0), defaultSong(1), clockSpeed(0), metadataHash(0), fileSize(0), lastModified(0), hvscNum(0) {}
    
    // Extraire les métadonnées depuis un SidTune
    static SidMetadata fromSidTune(const std::string& filepath, SidTune* tune);
    
    // Vérifier si le fichier a changé depuis l'indexation
    bool isFileChanged() const;
    
    // Générer un hash 32-bit basé sur les métadonnées (title+author+released+sidModel+clockSpeed)
    // SANS le path pour la compatibilité avec les ratings existants
    static uint32_t generateMetadataHash(const std::string& title, const std::string& author, 
                                         const std::string& released, const std::string& sidModel, 
                                         int clockSpeed);
};

// Spécialisation Glaze pour la sérialisation JSON
template <>
struct glz::meta<SidMetadata> {
    using T = SidMetadata;
    static constexpr auto value = glz::object(
        "filepath", &T::filepath,
        "filename", &T::filename,
        "title", &T::title,
        "author", &T::author,
        "released", &T::released,
        "sidModel", &T::sidModel,
        "numberOfSongs", &T::numberOfSongs,
        "defaultSong", &T::defaultSong,
        "clockSpeed", &T::clockSpeed,
        "infoStrings", &T::infoStrings,
        "metadataHash", &T::metadataHash,
        "md5Hash", &T::md5Hash,
        "fileSize", &T::fileSize,
        "lastModified", &T::lastModified,
        "hvscNum", &T::hvscNum,
        "rootFolder", &T::rootFolder,
        "songLengths", &T::songLengths
    );
};

#endif // SID_METADATA_H

