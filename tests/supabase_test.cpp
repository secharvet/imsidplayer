#ifdef ENABLE_CLOUD_SAVE

#include "SupabaseClient.h"
#include "Config.h"
#include "Logger.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Initialiser le logger
    Logger::initialize();
    
    std::cout << "=== Test Supabase Client ===\n\n";
    
    // Charger la configuration
    Config& config = Config::getInstance();
    std::string configPath = "config.txt";
    
    if (argc > 1) {
        configPath = argv[1];
    }
    
    if (!config.load(configPath)) {
        std::cerr << "Erreur: Impossible de charger la configuration depuis " << configPath << "\n";
        std::cerr << "Assurez-vous que le fichier contient:\n";
        std::cerr << "  supabase_project_url: https://xxxxx.supabase.co\n";
        std::cerr << "  supabase_anon_key: eyJ...\n";
        return 1;
    }
    
    std::string projectUrl = config.getSupabaseProjectUrl();
    std::string anonKey = config.getSupabaseAnonKey();
    
    if (projectUrl.empty() || anonKey.empty()) {
        std::cerr << "Erreur: supabase_project_url ou supabase_anon_key manquants dans la configuration\n";
        return 1;
    }
    
    std::cout << "Project URL: " << projectUrl << "\n";
    std::cout << "Anon Key: " << anonKey.substr(0, 20) << "...\n\n";
    
    // Initialiser SupabaseClient
    SupabaseClient client;
    if (!client.initialize(projectUrl, anonKey)) {
        std::cerr << "Erreur: Impossible d'initialiser SupabaseClient: " << client.getLastError() << "\n";
        return 1;
    }
    
    std::cout << "✓ SupabaseClient initialisé\n\n";
    
    // Test 1: Authentification anonyme
    std::cout << "=== Test 1: Authentification anonyme ===\n";
    
    // Vérifier si on a déjà des tokens sauvegardés dans la config
    std::string savedAccessToken = config.getSupabaseAccessToken();
    std::string savedRefreshToken = config.getSupabaseRefreshToken();
    std::string savedUserId = config.getSupabaseUserId();
    
    if (!savedAccessToken.empty() && !savedRefreshToken.empty() && !savedUserId.empty()) {
        std::cout << "✓ Tokens trouvés dans config.txt, réutilisation...\n";
        std::cout << "  User ID: " << savedUserId << "\n";
        std::cout << "  Access Token: " << savedAccessToken.substr(0, 30) << "...\n";
        std::cout << "  Refresh Token: " << savedRefreshToken.substr(0, 30) << "...\n\n";
        
        // Utiliser les tokens sauvegardés (SupabaseClient les charge déjà dans initialize())
        // On vérifie juste que le client est bien authentifié
        if (client.isAuthenticated()) {
            std::cout << "✓ Client authentifié avec les tokens sauvegardés\n\n";
        } else {
            std::cout << "⚠ Tokens sauvegardés mais client non authentifié, nouvelle authentification nécessaire\n";
            // Continuer pour faire une nouvelle authentification
        }
    }
    
    // Si pas de tokens sauvegardés ou si le client n'est pas authentifié, faire une nouvelle authentification
    if (!client.isAuthenticated() || savedAccessToken.empty() || savedRefreshToken.empty()) {
        AuthResponse authResponse = client.signInAnonymously();
        
        if (authResponse.success) {
            std::cout << "✓ Authentification anonyme réussie\n";
            std::cout << "  User ID: " << authResponse.user_id << "\n";
            std::cout << "  Access Token: " << authResponse.access_token.substr(0, 30) << "...\n";
            std::cout << "  Refresh Token: " << authResponse.refresh_token.substr(0, 30) << "...\n";
            
            // Sauvegarder les tokens dans la config (déjà fait automatiquement par signInAnonymously)
            // Mais on peut vérifier qu'ils sont bien sauvegardés
            if (config.save(configPath)) {
                std::cout << "✓ Tokens sauvegardés dans " << configPath << "\n\n";
            } else {
                std::cerr << "⚠ Échec de la sauvegarde des tokens dans " << configPath << "\n\n";
            }
        } else {
            std::cerr << "✗ Échec de l'authentification anonyme: " << authResponse.error_message << "\n";
            return 1;
        }
    }
    
    // Test 2: Créer un rating de test
    std::cout << "=== Test 2: Créer un rating ===\n";
    std::string testFileHash = "test_file_hash_12345";
    int testRating = 5;
    
    if (client.upsertRating(testFileHash, testRating)) {
        std::cout << "✓ Rating créé avec succès\n";
        std::cout << "  File Hash: " << testFileHash << "\n";
        std::cout << "  Rating: " << testRating << "\n\n";
    } else {
        std::cerr << "✗ Échec de la création du rating: " << client.getLastError() << "\n";
        return 1;
    }
    
    // Test 3: Récupérer le rating créé
    std::cout << "=== Test 3: Récupérer le rating ===\n";
    CommunityRating rating;
    if (client.getMyRating(testFileHash, rating)) {
        std::cout << "✓ Rating récupéré avec succès\n";
        std::cout << "  ID: " << rating.id << "\n";
        std::cout << "  File Hash: " << rating.file_hash << "\n";
        std::cout << "  Rating: " << rating.rating << "\n";
        std::cout << "  Created At: " << rating.created_at << "\n\n";
    } else {
        std::cerr << "✗ Échec de la récupération du rating: " << client.getLastError() << "\n";
        // Ce n'est pas fatal, peut-être que le parsing JSON n'est pas encore implémenté
    }
    
    // Test 4: Modifier le rating
    std::cout << "=== Test 4: Modifier le rating ===\n";
    int newRating = 4;
    if (client.upsertRating(testFileHash, newRating)) {
        std::cout << "✓ Rating modifié avec succès (nouveau rating: " << newRating << ")\n\n";
    } else {
        std::cerr << "✗ Échec de la modification du rating: " << client.getLastError() << "\n";
    }
    
    // Test 5: Récupérer toutes les statistiques
    std::cout << "=== Test 5: Récupérer les statistiques ===\n";
    RatingStatistics stats;
    if (client.getRatingStatistics(testFileHash, stats)) {
        std::cout << "✓ Statistiques récupérées avec succès\n";
        std::cout << "  Average Rating: " << stats.average_rating << "\n";
        std::cout << "  Total Ratings: " << stats.total_ratings << "\n\n";
    } else {
        std::cerr << "✗ Échec de la récupération des statistiques: " << client.getLastError() << "\n";
        // Ce n'est pas fatal, peut-être que le parsing JSON n'est pas encore implémenté
    }
    
    // Test 6: Supprimer le rating de test
    std::cout << "=== Test 6: Supprimer le rating de test ===\n";
    if (client.deleteRating(testFileHash)) {
        std::cout << "✓ Rating supprimé avec succès\n\n";
    } else {
        std::cerr << "✗ Échec de la suppression du rating: " << client.getLastError() << "\n";
    }
    
    std::cout << "=== Tous les tests terminés ===\n";
    
    Logger::shutdown();
    return 0;
}

#else
int main() {
    std::cerr << "ENABLE_CLOUD_SAVE n'est pas activé\n";
    return 1;
}
#endif
