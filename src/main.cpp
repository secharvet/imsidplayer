#include "Application.h"
#include "Logger.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Initialiser le logger en premier (avant Application)
    Logger::initialize();
    
    Application app;
    
    if (!app.initialize()) {
        LOG_CRITICAL_MSG("Impossible d'initialiser l'application");
        Logger::shutdown();
        return -1;
    }
    
    int result = app.run();
    app.shutdown();
    
    // Le logger sera arrêté par Application::shutdown()
    
    return result;
}

