#ifndef LOGGER_H
#define LOGGER_H

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/sinks/RotatingFileSink.h>
#include <quill/sinks/ConsoleSink.h>
#include <string>

// Wrapper simple autour de Quill pour faciliter l'utilisation
class Logger {
public:
    static void initialize(const std::string& logFilePath = "");
    static void shutdown();
    
    // Obtenir le logger par défaut
    static quill::Logger* getDefaultLogger() { return s_defaultLogger; }
    
    // Obtenir un logger spécifique par nom
    static quill::Logger* getLogger(const std::string& name);
    
private:
    static quill::Logger* s_defaultLogger;
    static bool s_initialized;
};

// Macros de logging simplifiées (redéfinir les macros Quill pour utiliser automatiquement le logger par défaut)
// On utilise #undef pour éviter les warnings de redéfinition
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif

#define LOG_TRACE(...) if (Logger::getDefaultLogger()) QUILL_LOG_TRACE(Logger::getDefaultLogger(), __VA_ARGS__)
#define LOG_DEBUG(...) if (Logger::getDefaultLogger()) QUILL_LOG_DEBUG(Logger::getDefaultLogger(), __VA_ARGS__)
#define LOG_INFO(...) if (Logger::getDefaultLogger()) QUILL_LOG_INFO(Logger::getDefaultLogger(), __VA_ARGS__)
#define LOG_WARNING(...) if (Logger::getDefaultLogger()) QUILL_LOG_WARNING(Logger::getDefaultLogger(), __VA_ARGS__)
#define LOG_ERROR(...) if (Logger::getDefaultLogger()) QUILL_LOG_ERROR(Logger::getDefaultLogger(), __VA_ARGS__)
#define LOG_CRITICAL_MSG(...) if (Logger::getDefaultLogger()) QUILL_LOG_CRITICAL(Logger::getDefaultLogger(), __VA_ARGS__)

#endif // LOGGER_H

