#include "Application.h"
#include <iostream>

int main(int argc, char* argv[]) {
    Application app;
    
    if (!app.initialize()) {
        std::cerr << "Erreur: Impossible d'initialiser l'application" << std::endl;
        return -1;
    }
    
    int result = app.run();
    app.shutdown();
    
    return result;
}



