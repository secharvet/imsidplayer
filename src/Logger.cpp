#include "Logger.h"
#include "Utils.h"
#include <filesystem>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

quill::Logger* Logger::s_defaultLogger = nullptr;
bool Logger::s_initialized = false;

void Logger::initialize(const std::string& logFilePath) {
    if (s_initialized) {
        return;
    }
    
    // Démarrer le backend Quill
    quill::Backend::start();
    
    // Déterminer le chemin du fichier de log
    std::string filePath = logFilePath;
    if (filePath.empty()) {
        fs::path configDir = getConfigDir();
        fs::path logDir = configDir / "logs";
        
        // Créer le dossier logs s'il n'existe pas
        if (!fs::exists(logDir)) {
            fs::create_directories(logDir);
        }
        
        // Nom du fichier simple (la rotation gère les backups avec date)
        filePath = (logDir / "imSidPlayer.log").string();
    }
    
    // Créer un sink rotatif pour le fichier (rotation automatique)
    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        filePath,
        []() {
            quill::RotatingFileSinkConfig cfg;
            cfg.set_open_mode('w');
            // Rotation à 10 MB
            cfg.set_rotation_max_file_size(10 * 1024 * 1024);
            // Garder maximum 5 fichiers de backup
            cfg.set_max_backup_files(5);
            // Schéma de nommage par date
            cfg.set_rotation_naming_scheme(quill::RotatingFileSinkConfig::RotationNamingScheme::Date);
            return cfg;
        }(),
        quill::FileEventNotifier{});
    
    // Créer le logger par défaut
    s_defaultLogger = quill::Frontend::create_or_get_logger(
        "default",
        std::move(file_sink),
        quill::PatternFormatterOptions{},
        quill::ClockSourceType::System);
    
    // Configurer le niveau de log selon le mode de build
    #ifdef ENABLE_DEBUG_LOGS
        s_defaultLogger->set_log_level(quill::LogLevel::Debug);
        QUILL_LOG_INFO(s_defaultLogger, "Niveau de log: DEBUG (ENABLE_DEBUG_LOGS défini)");
    #elif defined(NDEBUG)
        s_defaultLogger->set_log_level(quill::LogLevel::Info);
        QUILL_LOG_INFO(s_defaultLogger, "Niveau de log: INFO (NDEBUG défini)");
    #else
        s_defaultLogger->set_log_level(quill::LogLevel::Debug);
        QUILL_LOG_INFO(s_defaultLogger, "Niveau de log: DEBUG (par défaut)");
    #endif
    
    s_initialized = true;
    
    // Utiliser directement le logger pour le premier message
    QUILL_LOG_INFO(s_defaultLogger, "Logger initialisé - Fichier: {}", filePath);
    QUILL_LOG_DEBUG(s_defaultLogger, "Test log DEBUG - Si vous voyez ce message, le niveau DEBUG est actif");
}

void Logger::shutdown() {
    if (s_initialized) {
        quill::Backend::stop();
        s_initialized = false;
        s_defaultLogger = nullptr;
    }
}

quill::Logger* Logger::getLogger(const std::string& name) {
    if (!s_initialized) {
        initialize();
    }
    return quill::Frontend::create_or_get_logger(name);
}

