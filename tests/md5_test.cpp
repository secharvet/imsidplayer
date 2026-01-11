#include "MD5.h"
#include "Logger.h"
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;

// Helper function to calculate MD5 of a file
std::string calculateFileMd5(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return "";
    }
    
    MD5 md5;
    char buffer[4096];
    
    while (file.read(buffer, sizeof(buffer))) {
        md5.update(reinterpret_cast<unsigned char*>(buffer), file.gcount());
    }
    if (file.gcount() > 0) {
        md5.update(reinterpret_cast<unsigned char*>(buffer), file.gcount());
    }
    
    md5.finalize();
    return md5.toString();
}

// Helper function to calculate MD5 of a string
std::string calculateStringMd5(const std::string& str) {
    MD5 md5;
    md5.update(reinterpret_cast<const unsigned char*>(str.c_str()), str.length());
    md5.finalize();
    return md5.toString();
}

int main() {
    Logger::initialize();
    int failures = 0;
    int tests = 0;
    
    std::cout << "=== MD5 Implementation Tests ===\n\n";
    
    // Test 1: Empty string (RFC 1321 test vector)
    tests++;
    std::string md5_empty = calculateStringMd5("");
    std::string expected_empty = "d41d8cd98f00b204e9800998ecf8427e";
    if (md5_empty == expected_empty) {
        std::cout << "✓ Test 1 (empty string): PASSED\n";
    } else {
        std::cout << "✗ Test 1 (empty string): FAILED\n";
        std::cout << "  Got:      " << md5_empty << "\n";
        std::cout << "  Expected: " << expected_empty << "\n";
        failures++;
    }
    
    // Test 2: "a"
    tests++;
    std::string md5_a = calculateStringMd5("a");
    std::string expected_a = "0cc175b9c0f1b6a831c399e269772661";
    if (md5_a == expected_a) {
        std::cout << "✓ Test 2 ('a'): PASSED\n";
    } else {
        std::cout << "✗ Test 2 ('a'): FAILED\n";
        std::cout << "  Got:      " << md5_a << "\n";
        std::cout << "  Expected: " << expected_a << "\n";
        failures++;
    }
    
    // Test 3: "abc"
    tests++;
    std::string md5_abc = calculateStringMd5("abc");
    std::string expected_abc = "900150983cd24fb0d6963f7d28e17f72";
    if (md5_abc == expected_abc) {
        std::cout << "✓ Test 3 ('abc'): PASSED\n";
    } else {
        std::cout << "✗ Test 3 ('abc'): FAILED\n";
        std::cout << "  Got:      " << md5_abc << "\n";
        std::cout << "  Expected: " << expected_abc << "\n";
        failures++;
    }
    
    // Test 4: "message digest"
    tests++;
    std::string md5_msg = calculateStringMd5("message digest");
    std::string expected_msg = "f96b697d7cb7938d525a2f31aaf161d0";
    if (md5_msg == expected_msg) {
        std::cout << "✓ Test 4 ('message digest'): PASSED\n";
    } else {
        std::cout << "✗ Test 4 ('message digest'): FAILED\n";
        std::cout << "  Got:      " << md5_msg << "\n";
        std::cout << "  Expected: " << expected_msg << "\n";
        failures++;
    }
    
    // Test 5: "abcdefghijklmnopqrstuvwxyz"
    tests++;
    std::string md5_alpha = calculateStringMd5("abcdefghijklmnopqrstuvwxyz");
    std::string expected_alpha = "c3fcd3d76192e4007dfb496cca67e13b";
    if (md5_alpha == expected_alpha) {
        std::cout << "✓ Test 5 ('abcdefghijklmnopqrstuvwxyz'): PASSED\n";
    } else {
        std::cout << "✗ Test 5 ('abcdefghijklmnopqrstuvwxyz'): FAILED\n";
        std::cout << "  Got:      " << md5_alpha << "\n";
        std::cout << "  Expected: " << expected_alpha << "\n";
        failures++;
    }
    
    // Test 6: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    tests++;
    std::string md5_long = calculateStringMd5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    std::string expected_long = "d174ab98d277d9f5a5611c2c9f419d9f";
    if (md5_long == expected_long) {
        std::cout << "✓ Test 6 (long alphanumeric): PASSED\n";
    } else {
        std::cout << "✗ Test 6 (long alphanumeric): FAILED\n";
        std::cout << "  Got:      " << md5_long << "\n";
        std::cout << "  Expected: " << expected_long << "\n";
        failures++;
    }
    
    // Test 7: 12345.sid file (real SID file test)
    tests++;
    std::string sidFile = "12345.sid";
    if (fs::exists(sidFile)) {
        std::string md5_sid = calculateFileMd5(sidFile);
        std::string expected_sid = "2727236ead44a62f0c6e01f6dd4dc484";
        if (md5_sid == expected_sid) {
            std::cout << "✓ Test 7 (12345.sid file): PASSED\n";
            std::cout << "  Hash: " << md5_sid << "\n";
        } else {
            std::cout << "✗ Test 7 (12345.sid file): FAILED\n";
            std::cout << "  Got:      " << md5_sid << "\n";
            std::cout << "  Expected: " << expected_sid << "\n";
            failures++;
        }
    } else {
        std::cout << "⚠ Test 7 (12345.sid file): SKIPPED (file not found)\n";
    }
    
    // Test 8: Reset and reuse
    tests++;
    MD5 md5_test;
    md5_test.update(reinterpret_cast<const unsigned char*>("test"), 4);
    md5_test.finalize();
    std::string hash1 = md5_test.toString();
    md5_test.reset();
    md5_test.update(reinterpret_cast<const unsigned char*>("test"), 4);
    md5_test.finalize();
    std::string hash2 = md5_test.toString();
    if (hash1 == hash2 && hash1 == "098f6bcd4621d373cade4e832627b4f6") {
        std::cout << "✓ Test 8 (reset and reuse): PASSED\n";
    } else {
        std::cout << "✗ Test 8 (reset and reuse): FAILED\n";
        std::cout << "  Hash1: " << hash1 << "\n";
        std::cout << "  Hash2: " << hash2 << "\n";
        failures++;
    }
    
    std::cout << "\n=== Test Results ===\n";
    std::cout << "Tests run: " << tests << "\n";
    std::cout << "Passed: " << (tests - failures) << "\n";
    std::cout << "Failed: " << failures << "\n";
    
    Logger::shutdown();
    return (failures == 0) ? 0 : 1;
}

