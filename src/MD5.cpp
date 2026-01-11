/*
 * MD5 implementation based on Zunawe/md5-c
 * https://github.com/Zunawe/md5-c
 * 
 * Adapted to C++ class interface for imSid Player
 * This is a simple, commented reference implementation of the MD5 hash algorithm
 */

#include "MD5.h"
#include <cstring>
#include <iomanip>
#include <sstream>

// Constants defined by the MD5 algorithm
#define A 0x67452301
#define B 0xefcdab89
#define C 0x98badcfe
#define D 0x10325476

static uint32_t S[] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                       5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
                       4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                       6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static uint32_t K[] = {0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
                       0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                       0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
                       0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                       0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
                       0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                       0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
                       0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                       0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
                       0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                       0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
                       0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                       0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
                       0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                       0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
                       0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

static unsigned char PADDING[] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Bit-manipulation functions defined by the MD5 algorithm
#define F(X, Y, Z) ((X & Y) | (~X & Z))
#define G(X, Y, Z) ((X & Z) | (Y & ~Z))
#define H(X, Y, Z) (X ^ Y ^ Z)
#define I(X, Y, Z) (Y ^ (X | ~Z))

// Rotates a 32-bit word left by n bits
static uint32_t rotateLeft(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

MD5::MD5() {
    init();
}

void MD5::init() {
    _finalized = false;
    _count[0] = 0;
    _count[1] = 0;
    _state[0] = A;
    _state[1] = B;
    _state[2] = C;
    _state[3] = D;
    std::memset(_buffer, 0, sizeof(_buffer));
    std::memset(_digest, 0, sizeof(_digest));
}

void MD5::reset() {
    init();
}

void MD5::update(const unsigned char* input, size_t inputLen) {
    if (_finalized) {
        return;
    }
    
    uint32_t input_words[16];
    // Calculate current offset in bytes
    uint64_t size_in_bytes = ((uint64_t)_count[1] << 32) | _count[0];
    unsigned int offset = (unsigned int)(size_in_bytes % 64);
    
    // Update size (in bytes)
    size_in_bytes += inputLen;
    _count[0] = (uint32_t)(size_in_bytes & 0xFFFFFFFF);
    _count[1] = (uint32_t)((size_in_bytes >> 32) & 0xFFFFFFFF);
    
    // Copy each byte in input into the next space in our buffer
    for (size_t i = 0; i < inputLen; ++i) {
        _buffer[offset++] = (uint8_t)input[i];
        
        // If we've filled our buffer, copy it into our local array input_words
        // then reset the offset to 0 and fill in a new buffer.
        // Every time we fill out a chunk, we run it through the algorithm
        if (offset % 64 == 0) {
            for (unsigned int j = 0; j < 16; ++j) {
                // Convert to little-endian
                // The local variable `input_words` our 512-bit chunk separated into 32-bit words
                input_words[j] = (uint32_t)(_buffer[(j * 4) + 3]) << 24 |
                                (uint32_t)(_buffer[(j * 4) + 2]) << 16 |
                                (uint32_t)(_buffer[(j * 4) + 1]) <<  8 |
                                (uint32_t)(_buffer[(j * 4)]);
            }
            transform(_state, _buffer);
            offset = 0;
        }
    }
}

void MD5::finalize() {
    if (_finalized) {
        return;
    }
    
    uint32_t input_words[16];
    // Calculate current offset in bytes
    uint64_t total_size = ((uint64_t)_count[1] << 32) | _count[0];
    unsigned int offset = (unsigned int)(total_size % 64);
    unsigned int padding_length = offset < 56 ? 56 - offset : (56 + 64) - offset;
    
    // Fill in the padding (this will update _count, so we need to restore it)
    update(PADDING, padding_length);
    
    // Undo the size update from padding
    total_size = ((uint64_t)_count[1] << 32) | _count[0];
    total_size -= padding_length;
    _count[0] = (uint32_t)(total_size & 0xFFFFFFFF);
    _count[1] = (uint32_t)((total_size >> 32) & 0xFFFFFFFF);
    
    // Prepare final block with size in bits (little-endian)
    // The buffer now contains the padding, we need to write the size at the end
    for (unsigned int j = 0; j < 14; ++j) {
        input_words[j] = (uint32_t)(_buffer[(j * 4) + 3]) << 24 |
                        (uint32_t)(_buffer[(j * 4) + 2]) << 16 |
                        (uint32_t)(_buffer[(j * 4) + 1]) <<  8 |
                        (uint32_t)(_buffer[(j * 4)]);
    }
    input_words[14] = (uint32_t)(total_size * 8);
    input_words[15] = (uint32_t)((total_size * 8) >> 32);
    
    // Convert input_words to unsigned char array for transform (little-endian)
    unsigned char final_block[64];
    for (unsigned int j = 0; j < 16; ++j) {
        final_block[(j * 4) + 0] = (unsigned char)(input_words[j] & 0x000000FF);
        final_block[(j * 4) + 1] = (unsigned char)((input_words[j] & 0x0000FF00) >>  8);
        final_block[(j * 4) + 2] = (unsigned char)((input_words[j] & 0x00FF0000) >> 16);
        final_block[(j * 4) + 3] = (unsigned char)((input_words[j] & 0xFF000000) >> 24);
    }
    
    transform(_state, final_block);
    
    // Move the result into digest (little-endian)
    for (unsigned int i = 0; i < 4; ++i) {
        _digest[(i * 4) + 0] = (uint8_t)((_state[i] & 0x000000FF));
        _digest[(i * 4) + 1] = (uint8_t)((_state[i] & 0x0000FF00) >>  8);
        _digest[(i * 4) + 2] = (uint8_t)((_state[i] & 0x00FF0000) >> 16);
        _digest[(i * 4) + 3] = (uint8_t)((_state[i] & 0xFF000000) >> 24);
    }
    
    // Zeroize sensitive information
    std::memset(_buffer, 0, sizeof(_buffer));
    std::memset(_count, 0, sizeof(_count));
    
    _finalized = true;
}

std::string MD5::toString() const {
    if (!_finalized) {
        return "";
    }
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        ss << std::setw(2) << (int)_digest[i];
    }
    return ss.str();
}

void MD5::transform(uint32_t state[4], const unsigned char block[64]) {
    uint32_t input[16];
    
    // Convert block to 32-bit words (little-endian)
    for (unsigned int j = 0; j < 16; ++j) {
        input[j] = (uint32_t)(block[(j * 4) + 3]) << 24 |
                   (uint32_t)(block[(j * 4) + 2]) << 16 |
                   (uint32_t)(block[(j * 4) + 1]) <<  8 |
                   (uint32_t)(block[(j * 4)]);
    }
    
    uint32_t AA = state[0];
    uint32_t BB = state[1];
    uint32_t CC = state[2];
    uint32_t DD = state[3];
    
    uint32_t E;
    unsigned int j;
    
    for (unsigned int i = 0; i < 64; ++i) {
        switch (i / 16) {
            case 0:
                E = F(BB, CC, DD);
                j = i;
                break;
            case 1:
                E = G(BB, CC, DD);
                j = ((i * 5) + 1) % 16;
                break;
            case 2:
                E = H(BB, CC, DD);
                j = ((i * 3) + 5) % 16;
                break;
            default:
                E = I(BB, CC, DD);
                j = (i * 7) % 16;
                break;
        }
        
        uint32_t temp = DD;
        DD = CC;
        CC = BB;
        BB = BB + rotateLeft(AA + E + K[i] + input[j], S[i]);
        AA = temp;
    }
    
    state[0] += AA;
    state[1] += BB;
    state[2] += CC;
    state[3] += DD;
    
    std::memset(input, 0, sizeof(input));
}

void MD5::encode(unsigned char* output, const uint32_t* input, size_t len) {
    // Not used in this implementation, kept for interface compatibility
    (void)output;
    (void)input;
    (void)len;
}

void MD5::decode(uint32_t* output, const unsigned char* input, size_t len) {
    // Not used in this implementation, kept for interface compatibility
    (void)output;
    (void)input;
    (void)len;
}
