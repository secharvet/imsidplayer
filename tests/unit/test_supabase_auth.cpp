#ifdef ENABLE_CLOUD_SAVE

#include <gtest/gtest.h>
#include "SupabaseClient.h"
#include "Config.h"
#include "Logger.h"
#include <string>
#include <fstream>
#include <ctime>

// Configuration de test
const std::string TEST_CONFIG_FILE = "tests/config_test.txt";
const std::string TEST_PROJECT_URL = "https://mizxbtqltozqhuhanqyy.supabase.co";
const std::string TEST_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im1penhidHFsdG96cWh1aGFucXl5Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3Njk4NTQwOTYsImV4cCI6MjA4NTQzMDA5Nn0.FtOC4bc3X3uv2VfHPCtfVH9hVhTuYhQY2e1ZOx4XKqo";

class SupabaseClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::initialize();
    }
    
    void TearDown() override {
        Logger::shutdown();
    }
};

// Tests d'initialisation
TEST_F(SupabaseClientTest, ValidInitialization) {
    SupabaseClient client;
    EXPECT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    EXPECT_TRUE(client.getLastError().empty());
}

TEST_F(SupabaseClientTest, InvalidURL) {
    SupabaseClient client;
    EXPECT_FALSE(client.initialize("", TEST_ANON_KEY));
    EXPECT_FALSE(client.getLastError().empty());
}

TEST_F(SupabaseClientTest, InvalidAPIKey) {
    SupabaseClient client;
    EXPECT_FALSE(client.initialize(TEST_PROJECT_URL, ""));
    EXPECT_FALSE(client.getLastError().empty());
}

// Tests d'authentification anonyme
TEST_F(SupabaseClientTest, AnonymousSignIn) {
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    
    AuthResponse response = client.signInAnonymously();
    
    EXPECT_TRUE(response.success);
    EXPECT_FALSE(response.access_token.empty());
    EXPECT_FALSE(response.user_id.empty());
    EXPECT_TRUE(client.isAuthenticated());
    EXPECT_FALSE(client.getAccessToken().empty());
}

TEST_F(SupabaseClientTest, MultipleAnonymousSignIns) {
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    
    AuthResponse response1 = client.signInAnonymously();
    EXPECT_TRUE(response1.success);
    
    // Sign out
    client.signOut();
    EXPECT_FALSE(client.isAuthenticated());
    
    // Sign in again
    AuthResponse response2 = client.signInAnonymously();
    EXPECT_TRUE(response2.success);
    EXPECT_FALSE(response2.user_id.empty());
}

// Tests de gestion des tokens
TEST_F(SupabaseClientTest, TokenManagement) {
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    
    std::string testToken = "test_token_12345";
    client.setAccessToken(testToken);
    
    EXPECT_TRUE(client.isAuthenticated());
    EXPECT_EQ(client.getAccessToken(), testToken);
}

TEST_F(SupabaseClientTest, SignOutClearsToken) {
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    
    AuthResponse response = client.signInAnonymously();
    ASSERT_TRUE(response.success);
    EXPECT_TRUE(client.isAuthenticated());
    
    client.signOut();
    EXPECT_FALSE(client.isAuthenticated());
    EXPECT_TRUE(client.getAccessToken().empty());
}

// Tests d'authentification email/password
TEST_F(SupabaseClientTest, EmailSignUp) {
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    
    // Générer un email unique
    std::string uniqueEmail = "test_" + std::to_string(time(nullptr)) + "@example.com";
    std::string password = "test_password_123";
    
    AuthResponse response = client.signUpWithEmail(uniqueEmail, password);
    
    // Peut réussir ou échouer selon la configuration Supabase
    if (response.success) {
        EXPECT_FALSE(response.access_token.empty());
        EXPECT_FALSE(response.user_id.empty());
        EXPECT_TRUE(client.isAuthenticated());
    } else {
        EXPECT_FALSE(response.error_message.empty());
    }
}

TEST_F(SupabaseClientTest, InvalidCredentials) {
    SupabaseClient client;
    ASSERT_TRUE(client.initialize(TEST_PROJECT_URL, TEST_ANON_KEY));
    
    AuthResponse response = client.signInWithEmail("nonexistent@example.com", "wrong_password");
    
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.error_message.empty());
}

#endif // ENABLE_CLOUD_SAVE
