#ifndef ABOM_H
#define ABOM_H

#include "llvm/ADT/ArrayRef.h"
#include <cstdint>
#include <string>
#include <bitset>
#include <memory>
#include <vector>
#include <cmath>

// Number of elements in Bloom filter, expressed in log_2
#define ABOM_LOG2_M 18
// Number of hash functions in Bloom filter
#define ABOM_K 2
// The highest tolerated false positive rate
#define ABOM_F std::pow((double)2, (double)-14) // 2^-14
// Constant for number of bits used to encode the CDF
#define ABOM_CDF_BITS 16
// Buffer size for reading files
#define ABOM_BUF_SIZE 8192


#define BITWIDTH 8
#define BYTES(x) (x / BITWIDTH + (x % BITWIDTH > 0))
#define ABOM_M (1<<ABOM_LOG2_M) // equivalent to 2^ABOM_LOG2_M
#define ABOM_BITS (ABOM_LOG2_M*ABOM_K)

using hash_t = uint8_t[BYTES(ABOM_BITS)];

namespace llvm {
namespace ABOM {

    class CBloomFilter {
        public:
            // The Bloom filter
            std::shared_ptr<std::bitset<ABOM_M>> bf;

            /// @brief Default Constructor
            CBloomFilter();

            /// @brief Duplicating Constructor
            /// @param bf The pre-existing Bloom filter to copy
            CBloomFilter(std::bitset<ABOM_M> preexist);

            /// @brief Deep copy factory function
            /// @param other The filter to copy
            CBloomFilter clone();

            /// @brief Calculates the false positive rate of the filter
            /// @return The false positive rate
            double falsePositiveRate();

            /// @brief Insert a hash into the filter
            /// @param hash The hash to insert
            void insert(hash_t hash);

            /// @brief Check if the filter likely contains a hash
            /// @param hash The hash to check
            /// @return True if the filter likely contains the
            /// hash, false otherwise
            bool contains(hash_t hash);

            /// @brief Union operator. Does not modify operands.
            /// @param rhs The filter to union with
            /// @return The union of the two filters
            const CBloomFilter operator|(const CBloomFilter &rhs);

        protected:
            /// @brief Calculate the indices pointed to by the hash
            /// @param hash The hash for which to calculate indices
            /// @param idxs The array to store the indices
            void indices(hash_t hash, uint32_t *idxs);

    };

    class ABOM {
        public:
            /// @brief Insert a hash into the ABOM
            /// @param hash The hash to insert
            void insertHash(hash_t hash);

            /// @brief Insert a file's hash into the ABOM
            /// @param filename The file to hash and insert
            void insertFile(std::string filename);

            /// @brief Check if the ABOM likely contains a hash
            /// @param hash The hash to check
            /// @return True if the ABOM likely contains the
            /// hash, false otherwise
            bool contains(hash_t hash);

            /// @brief Serialize the ABOM to a buffer
            /// @return The serialized ABOM
            std::vector<uint8_t> serialize();

            /// @brief Union operator. Modifies the left operand.
            /// @param rhs The ABOM to union with
            void operator|=(const ABOM &rhs);

            /// @brief Union operator. Does not modify operands.
            /// @param rhs The ABOM to union with
            /// @return The union of the two ABOMs
            const ABOM operator|(const ABOM &rhs);

            /// @brief Deserialize an ABOM from a buffer
            /// @remarks On error, empty ABOM is returned
            /// and an error message is printed to stderr
            /// @param buffer The buffer to deserialize
            /// @param filename Optional. The filename
            /// containing the ABOM, for error reporting only.
            /// @return The deserialized ABOM
            static ABOM deserialize(llvm::ArrayRef<uint8_t> buffer,
                                    std::string const &filename = std::string());

            // Bloom filters
            std::vector<CBloomFilter> bfs;
    };

    struct ABOMHeader {
        // Protocol magic word
        char magic[4] = {'A', 'B', 'O', 'M'};
        // Protocol version
        uint8_t version = 1;
        // Number of Bloom filters `n`
        uint16_t numFilters;
        // Arithmetic Model p(1) of concatenated Bloom
        // filters as '[0,1] x (2^32-1)'
        uint32_t model;
        // Byte length of Compressed Bloom Filters Blob `l`
        uint32_t blobSize;
    };

namespace SHAKE128 {

    /// @brief Convert a hash to a hexadecimal string
    /// @param data The hash to convert
    /// @param bits The number of bits in the hash
    /// @return The hash as a hexadecimal string
    std::string toHex(hash_t data, uint8_t bits = ABOM_BITS);

    /// @brief Hash a message using SHAKE128
    /// @param msg The message to hash
    /// @param bits The number of bits to emit for the hash
    /// @param out The output buffer
    /// @return True if the hash was successful, false otherwise
    bool hash(std::string msg, uint8_t *out, uint8_t bits = ABOM_BITS);

    /// @brief Hash a file using SHAKE128
    /// @param filename The file to hash
    /// @param bits The number of bits to emit for the hash
    /// @param out The output buffer
    /// @return True if the hash was successful, false otherwise
    bool hashFile(std::string filename, uint8_t *out, uint8_t bits = ABOM_BITS);
}
}
}

#endif