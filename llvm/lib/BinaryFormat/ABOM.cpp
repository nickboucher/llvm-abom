#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/BinaryFormat/ArithmeticCoding.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/ArrayRef.h"
#include <bitset>
#include <string>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <numeric>
#include <vector>
#include <memory>
#include <cmath>
#include <openssl/evp.h>

using namespace llvm::ABOM;

void ABOM::insertHash(hash_t hash) {
    for (size_t i=0; i<bfs.size(); i++) {
        // Check false positive rate
        if (bfs[i].falsePositiveRate() <ABOM_F) {
            bfs[i].insert(hash);
            return;
        }
    }
    // If no filter has a low enough false
    // positive rate, create a new one
    CBloomFilter bf = bfs.emplace_back(CBloomFilter());
    bf.insert(hash);
}

void ABOM::insertFile(std::string filename) {
    hash_t digest;
    if (SHAKE128::hashFile(filename, digest)) {
        insertHash(digest);
    }
}

bool ABOM::contains(hash_t hash) {
    for (size_t i=0; i<bfs.size(); i++) {
        if (bfs[i].contains(hash)) {
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> ABOM::serialize() {
    ABOMHeader header;
    auto enc = llvm::AC::ArithmeticCodingEncoder<uint64_t, uint32_t>(32);
    uint64_t acc = 0;
    for (size_t i=0; i< bfs.size(); i++) {
        acc += bfs[i].bf->count();
    }
    double p1 = (double)acc / (double)(ABOM_M * bfs.size());
    uint32_t cdfMax = 1 << ABOM_CDF_BITS; // 2^ABOM_CDF_BITS
    uint32_t cdf[3] = {0, (uint32_t)((1.0 - p1) * cdfMax), cdfMax};
    for (size_t i=0; i<bfs.size(); i++) {
        for (int j=0; j<ABOM_M; j++) {
            enc.encode(bfs[i].bf->test(j) ? 1 : 0, cdf, ABOM_CDF_BITS);
        }
    }
    enc.flush();
    header.numFilters = bfs.size();
    header.model = p1 * UINT32_MAX;
    header.blobSize = enc.bit_stream._data.size();
    for (size_t i=0; i<sizeof(ABOMHeader); i++) {
        enc.bit_stream._data.insert(enc.bit_stream._data.begin() + i, ((uint8_t *)&header)[i]);
    }
    return enc.bit_stream._data;
}

void ABOM::operator|=(const ABOM &rhs) {
    for (size_t i=0; i<bfs.size(); i++) {
        bool inserted = false;
        for (size_t j=0; j<rhs.bfs.size(); j++) {
            CBloomFilter bf = bfs[i] | rhs.bfs[j];
            if (bf.falsePositiveRate() < ABOM_F) {
                bfs[j] = bf;
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            bfs.push_back(rhs.bfs[i]);
        }
    }
}

const ABOM ABOM::operator|(const ABOM &rhs) {
    ABOM result;
    result |= rhs;
    return result;
}

// Anonymous namespace to prevent external access
namespace {
inline ABOM deserializeError(std::string const &error, std::string const &filename) {
    llvm::errs() << error;
    if (!filename.empty()) {
        llvm::errs() << "in: " << filename;
    }
    llvm::errs() << "\n";
    return ABOM();
}
}

ABOM ABOM::deserialize(llvm::ArrayRef<uint8_t> buffer, std::string const &filename) {
    ABOMHeader header;
    for (size_t i=0; i<sizeof(ABOMHeader); i++) {
        ((uint8_t *)&header)[i] = buffer[i];
    }
    if (header.magic[0] != 'A' || header.magic[1] != 'B' ||
        header.magic[2] != 'O' || header.magic[3] != 'M') {
        return deserializeError("Invalid ABOM header", filename);
    }
    if (header.version != 1) {
        return deserializeError("Unsupported ABOM version", filename);
    }
    uint32_t cdfMax = 1 << ABOM_CDF_BITS; // 2^ABOM_CDF_BITS
    uint32_t cdf[3] = {0, (uint32_t)((1.0 - (double)header.model / (double)UINT32_MAX) * cdfMax), cdfMax};
    llvm::AC::BitStream bs;
    bs._data = std::vector<uint8_t>(buffer.begin() + sizeof(ABOMHeader), buffer.end());
    bs._pos = header.blobSize * 8;
    auto dec = llvm::AC::ArithmeticCodingDecoder<uint64_t, uint32_t>(32, bs);
    ABOM result;
    for (int i=0; i<header.numFilters; i++) {
        CBloomFilter bf;
        for (int j=0; j<ABOM_M; j++) {
            bf.bf->set(j, dec.decode(2, cdf, ABOM_CDF_BITS));
        }
        result.bfs.push_back(bf);
    }
    return result;
}

CBloomFilter::CBloomFilter() :
        bf(std::make_shared<std::bitset<ABOM_M>>()) {}

CBloomFilter::CBloomFilter(std::bitset<ABOM_M> preexist) :
        bf(std::make_shared<std::bitset<ABOM_M>>(preexist)) {}

CBloomFilter CBloomFilter::clone() {
    return CBloomFilter(*bf);
}

double CBloomFilter::falsePositiveRate() {
    return pow((double)bf->count() / (double)ABOM_M, ABOM_K);
}

void CBloomFilter::insert(hash_t hash) {
    uint32_t idxs[ABOM_K];
    indices(hash, idxs);
    for (int idx : idxs) {
        bf->set(idx, 1);
    }
}

bool CBloomFilter::contains(hash_t hash) {
    uint32_t idxs[ABOM_K];
    indices(hash, idxs);
    for (uint32_t idx : idxs) {
        if (!(*bf)[idx]) {
            return false;
        }
    }
    return true;
}

const CBloomFilter CBloomFilter::operator|(const CBloomFilter &rhs) {
    return CBloomFilter(*bf | *rhs.bf);
}

void CBloomFilter::indices(hash_t hash, uint32_t *idxs) {
    for (int k=0; k<ABOM_K; k++) {
        uint32_t idx = 0;
        uint8_t cursorBit = (k * ABOM_LOG2_M) % BITWIDTH;
        uint32_t cursorByte = (k * ABOM_LOG2_M) / BITWIDTH;
        uint32_t i = 0;
        while (i < ABOM_LOG2_M) {
            idx = (idx << 1) + !!(((hash[cursorByte] << cursorBit) & 0x80));
            if (++cursorBit == BITWIDTH) {
                cursorBit = 0;
                cursorByte++;
            }
            i++;
        }
        idxs[k] = idx;
    }
}

std::string SHAKE128::toHex(hash_t data, uint8_t bits) {
    std::stringstream ss;
    std::string s;
    uint8_t partialByte = bits % BITWIDTH;

    for (int i=0; i<BYTES(bits); i++) {
        ss << std::setw(2) << std::setfill('0') << std::hex << (int)data[i];
    }
    
    // Remove trailing zero if it's a partial byte
    s = ss.str();
    if (partialByte && partialByte <= 4) {
        s.pop_back();
    }

    return s;
}

// Anonymous namespace to prevent external access
namespace {
bool initializeHash(EVP_MD **md, EVP_MD_CTX **ctx, uint8_t bits) {
    // Local variables
    OSSL_PARAM *params;
    std::string len = std::to_string(BYTES(bits));
    // Initialize the hash function
    if (!(*md = EVP_MD_fetch(NULL, "shake128", NULL))) return false;
    if (!(*ctx = EVP_MD_CTX_new())) return false;
    if (EVP_DigestInit_ex(*ctx, *md, NULL) != 1) return false;
    // Set hash function parameters
    params = (OSSL_PARAM *)OPENSSL_zalloc(sizeof(OSSL_PARAM) * 2);
    OSSL_PARAM_allocate_from_text(&params[0], EVP_MD_settable_ctx_params(*md),
                                           "xoflen", len.c_str(), len.size(), NULL);
    params[1] = OSSL_PARAM_construct_end();
    if (EVP_MD_CTX_set_params(*ctx, params) != 1) return false;
    for (int i = 0; params[i].key != NULL; ++i) {
        OPENSSL_free(params[i].data);
    }
    OPENSSL_free(params);
    // Initialization successful
    return true;
}

bool finalizeHash(EVP_MD **md, EVP_MD_CTX **ctx, hash_t out, uint8_t bits) {
    // Local variables
    unsigned int digest_len;
    uint8_t partialByte;
    // Finalize the hash
    if (EVP_DigestFinal_ex(*ctx, out, &digest_len) != 1) return false;
    // Clean up
    EVP_MD_CTX_free(*ctx);
    EVP_MD_free(*md);
    // Remove excess bits if not byte-aligned
    partialByte = bits % BITWIDTH;
    if (partialByte) {
        out[digest_len-1] &= (0xff << (BITWIDTH-partialByte));
    }
    // Finalization successful
    return true;
}

bool hashError(std::string target) {
    llvm::errs() << "Error calculating SHAKE128 hash: " << target << "\n";
    return false;
}
}

bool SHAKE128::hash(std::string msg, hash_t out, uint8_t bits) {
    EVP_MD *md;
    EVP_MD_CTX *ctx;
    if (!initializeHash(&md, &ctx, bits)) return hashError(msg);
    if (!EVP_DigestUpdate(ctx, msg.c_str(), msg.length())) return hashError(msg);
    if (!finalizeHash(&md, &ctx, out, bits)) return hashError(msg);
    return true;
}

bool SHAKE128::hashFile(std::string filename, hash_t out, uint8_t bits) {
    EVP_MD *md;
    EVP_MD_CTX *ctx;
    std::unique_ptr<uint8_t[]> buf = std::make_unique<uint8_t[]>(ABOM_BUF_SIZE);
    std::ifstream file(filename, std::ios::binary);

    if (file.fail()) return hashError("Could not open file:" + filename);
    if (!initializeHash(&md, &ctx, bits)) return hashError(filename);

    do {
        file.read((char *)buf.get(), ABOM_BUF_SIZE);
        if (EVP_DigestUpdate(ctx, buf.get(), file.gcount()) != 1) return hashError(filename);
    } while (!file.eof());

    if (!finalizeHash(&md, &ctx, out, bits)) return hashError(filename);
    return true;
}