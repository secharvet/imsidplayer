#ifndef MD5_H
#define MD5_H

#include <string>
#include <cstdint>

namespace imsid {

/**
 * MD5 hash implementation (RFC 1321)
 * 
 * Simple, lightweight MD5 implementation with no external dependencies.
 * Used for calculating MD5 hashes of SID files for Songlengths.md5 support.
 */
class ImSidMD5 {
public:
    ImSidMD5();
    
    /**
     * Update the hash with new data
     * @param input Pointer to input data
     * @param inputLen Length of input data in bytes
     */
    void update(const unsigned char* input, size_t inputLen);
    
    /**
     * Finalize the hash computation
     * Must be called after all data has been added via update()
     */
    void finalize();
    
    /**
     * Get the hash as a 32-character hexadecimal string
     * @return MD5 hash in hex format (lowercase)
     */
    std::string toString() const;
    
    /**
     * Get the digest as raw bytes (16 bytes)
     * @param output Buffer to store the 16 bytes digest
     */
    void getDigest(unsigned char* output) const;
    
    /**
     * Reset the hash to initial state
     * Allows reuse of the same MD5 object
     */
    void reset();

private:
    void init();
    void transform(uint32_t state[4], const unsigned char block[64]);
    static void encode(unsigned char* output, const uint32_t* input, size_t len);
    static void decode(uint32_t* output, const unsigned char* input, size_t len);

    uint32_t _state[4];        // State (ABCD)
    uint32_t _count[2];        // Number of bits, modulo 2^64 (lsb first)
    unsigned char _buffer[64]; // Input buffer
    unsigned char _digest[16];  // Message digest
    bool _finalized;            // True if finalize() has been called
};

} // namespace imsid

#endif // MD5_H




