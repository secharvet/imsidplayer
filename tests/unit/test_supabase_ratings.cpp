#ifdef ENABLE_CLOUD_SAVE

#include <gtest/gtest.h>
#include "SupabaseClient.h"
#include "Logger.h"
#include <string>
#include <ctime>
#include <cstdlib>

const std::string TEST_PROJECT_URL = "https://mizxbtqltozqhuhanqyy.supabase.co";
const std::string TEST_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im1penhidHFsdG96cWh1aGFucXl5Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3Njk4NTQwOTYsImV4cCI6MjA4NTQzMDA5Nn0.FtOC4bc3X3uv2VfHPCtfVH9hVhTuYhQY2e1ZOx4XKqo";

// Helper pour générer un hash unique
std::string generateUniqueHash() {
    return "test_hash_" + std::to_string(time(nullptr)) + "_" + std::to_string(rand());
}

class SupabaseRatingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::initialize();
        
        // Réutiliser l'authentification entre les tests pour éviter le rate limit
        static bool authenticated = false;
        
        if (!authenticated) {
            ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
            AuthResponse authResponse = client.signInAnonymously();
            if (!authResponse.success) {
                std::cerr << "Auth failed: " << authResponse.error_message << std::endl;
                std::cerr << "Last error: " << client.getLastError() << std::endl;
            }
            ASSERT_TRUE(authResponse.success) << "Authentication failed: " << authResponse.error_message;
            authenticated = true;
        } else {
            // Réutiliser le client existant (réinitialiser avec le même token depuis config)
            ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
            // Le JWT sera chargé automatiquement depuis la config
        }
    }
    
    void TearDown() override {
        // Ne pas sign out pour réutiliser la session
        Logger::shutdown();
    }
    
    SupabaseClient client;
};

// Tests CRUD des ratings
TEST_F(SupabaseRatingsTest, CreateRating) {
    std::string testFileHash = generateUniqueHash();
    int testRating = 5;
    
    EXPECT_TRUE(client.upsertRating(testFileHash, testRating));
    EXPECT_TRUE(client.getLastError().empty());
    
    // Nettoyage
    client.deleteRating(testFileHash);
}

TEST_F(SupabaseRatingsTest, UpdateRating) {
    std::string testFileHash = generateUniqueHash();
    
    // Créer un rating
    EXPECT_TRUE(client.upsertRating(testFileHash, 5));
    
    // Modifier le rating
    EXPECT_TRUE(client.upsertRating(testFileHash, 4));
    
    // Nettoyage
    client.deleteRating(testFileHash);
}

TEST_F(SupabaseRatingsTest, DeleteRating) {
    std::string testFileHash = generateUniqueHash();
    
    // Créer un rating
    EXPECT_TRUE(client.upsertRating(testFileHash, 5));
    
    // Supprimer le rating
    EXPECT_TRUE(client.deleteRating(testFileHash));
}

// Tests de validation
TEST_F(SupabaseRatingsTest, InvalidRatingTooLow) {
    std::string testFileHash = generateUniqueHash();
    
    EXPECT_FALSE(client.upsertRating(testFileHash, 0));
    EXPECT_FALSE(client.getLastError().empty());
}

TEST_F(SupabaseRatingsTest, InvalidRatingTooHigh) {
    std::string testFileHash = generateUniqueHash();
    
    EXPECT_FALSE(client.upsertRating(testFileHash, 6));
    EXPECT_FALSE(client.getLastError().empty());
}

TEST_F(SupabaseRatingsTest, ValidRatings) {
    for (int rating = 1; rating <= 5; ++rating) {
        std::string hash = generateUniqueHash();
        EXPECT_TRUE(client.upsertRating(hash, rating));
        
        // Nettoyage
        client.deleteRating(hash);
    }
}

// Test sans authentification
TEST(SupabaseRatingsTestUnauth, CannotCreateRatingWithoutAuth) {
    Logger::initialize();
    
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    // Ne pas s'authentifier
    
    std::string testFileHash = generateUniqueHash();
    
    EXPECT_FALSE(client.upsertRating(testFileHash, 5));
    EXPECT_FALSE(client.getLastError().empty());
    EXPECT_NE(client.getLastError().find("authenticated"), std::string::npos);
    
    Logger::shutdown();
}

#endif // ENABLE_CLOUD_SAVE
