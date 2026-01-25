#ifdef ENABLE_CLOUD_SAVE

#include "HTTPClient.h"
#include "Logger.h"
#include "Utils.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // Initialiser le logger avec niveau DEBUG
    Logger::initialize();
    quill::Logger* logger = Logger::getDefaultLogger();
    if (logger) {
        logger->set_log_level(quill::LogLevel::Debug);
    }
    
    std::string testEndpoint;
    if (argc > 1) {
        testEndpoint = argv[1];
    } else {
        testEndpoint = "https://api.npoint.io/0316f236087020c9dcc1";
        std::cout << "Usage: " << argv[0] << " <endpoint_url>" << std::endl;
        std::cout << "Using default endpoint: " << testEndpoint << std::endl;
    }
    
    std::cout << "\n=== Test HTTPClient avec MBed TLS ===" << std::endl;
    std::cout << "Endpoint: " << testEndpoint << std::endl;
    std::cout << "=====================================\n" << std::endl;
    
    // Créer le client HTTP
    HTTPClient client;
    
    // Test 1: Initialisation
    std::cout << "[TEST 1] Initialisation de HTTPClient..." << std::endl;
    if (!client.initialize()) {
        std::cerr << "❌ Échec de l'initialisation: " << client.getLastError() << std::endl;
        return 1;
    }
    std::cout << "✅ HTTPClient initialisé avec succès" << std::endl;
    
    // Charger le Root CA
    std::cout << "\n[TEST 2] Chargement du Root CA..." << std::endl;
    fs::path caPath;
    std::vector<fs::path> possiblePaths = {
        fs::current_path().parent_path() / "certs" / "google_trust_services_bundle.pem",
        fs::current_path() / "certs" / "google_trust_services_bundle.pem",
        fs::path(__FILE__).parent_path().parent_path() / "certs" / "google_trust_services_bundle.pem"
    };
    
    bool caLoaded = false;
    for (const auto& path : possiblePaths) {
        if (fs::exists(path)) {
            caPath = fs::canonical(path);
            if (client.loadRootCA(caPath.string())) {
                std::cout << "✅ Root CA chargé depuis: " << caPath.string() << std::endl;
                caLoaded = true;
                break;
            }
        }
    }
    
    if (!caLoaded) {
        std::cout << "⚠️  Root CA non trouvé, tests continueront sans validation de certificat" << std::endl;
    }
    
    // Test 3: GET
    std::cout << "\n[TEST 3] Test GET..." << std::endl;
    std::cout << "  → Envoi de GET vers " << testEndpoint << std::endl;
    auto getResponse = client.get(testEndpoint);
    
    std::cout << "  → Code de statut: " << getResponse.statusCode << std::endl;
    std::cout << "  → Taille du body: " << getResponse.body.length() << " bytes" << std::endl;
    std::cout << "  → Nombre de headers: " << getResponse.headers.size() << std::endl;
    
    if (!client.getLastError().empty() && client.getLastError() != "No error") {
        std::cout << "  ⚠️  Erreur HTTPClient: " << client.getLastError() << std::endl;
    }
    
    if (getResponse.statusCode == 200) {
        std::cout << "  ✅ GET réussi" << std::endl;
        std::cout << "  → Body (premiers 200 chars): " << getResponse.body.substr(0, 200) << std::endl;
    } else {
        std::cout << "  ❌ GET échoué (code: " << getResponse.statusCode << ")" << std::endl;
        std::cout << "  → Body: " << getResponse.body.substr(0, 500) << std::endl;
    }
    
    /* 
    // TEST 4: POST (MISE À JOUR) - DÉSACTIVÉ POUR ÉVITER D'ÉCRASER LES DONNÉES RÉELLES
    std::cout << "\n[TEST 4] Test POST (mise à jour) - DÉSACTIVÉ" << std::endl;
    */
    
    // Test 5: GET (vérification)
    std::cout << "\n[TEST 5] Test GET (vérification)..." << std::endl;
    auto getResponse2 = client.get(testEndpoint);
    
    std::cout << "  → Code de statut: " << getResponse2.statusCode << std::endl;
    std::cout << "  → Taille du body: " << getResponse2.body.length() << " bytes" << std::endl;
    
    if (getResponse2.statusCode == 200) {
        std::cout << "  ✅ GET réussi" << std::endl;
    } else {
        std::cout << "  ❌ GET échoué (code: " << getResponse2.statusCode << ")" << std::endl;
    }
    
    // Test 6: Affichage des headers
    std::cout << "\n[TEST 6] Headers de la dernière réponse GET..." << std::endl;
    for (const auto& [key, value] : getResponse2.headers) {
        std::cout << "  → " << key << ": " << value << std::endl;
    }
    
    // Nettoyage
    std::cout << "\n[TEST 7] Nettoyage..." << std::endl;
    client.cleanup();
    std::cout << "  ✅ HTTPClient nettoyé" << std::endl;
    
    std::cout << "\n=== Tests terminés ===" << std::endl;
    
    return 0;
}

#else
#include <iostream>
int main() {
    std::cerr << "Cloud save is not enabled. Compile with -DENABLE_CLOUD_SAVE=ON" << std::endl;
    return 1;
}
#endif
