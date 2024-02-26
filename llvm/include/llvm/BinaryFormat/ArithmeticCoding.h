/*
Based on YAECL: Yet Another Entropy Coding Library for Neural Compression Research
by Tongda Xu, et al. (2022)
Under MIT License
*/
#ifndef ARITHMETIC_CODING_H
#define ARITHMETIC_CODING_H

#include <vector>
#include <cstdint>
#include <string>

namespace llvm {
namespace AC {

class BitStream {
    public:
        std::vector<uint8_t> _data;
        int _pos;
        int _fpos;

        BitStream();
        void push_back(bool bit);
        void push_back_byte(uint8_t byte);
        bool get(int pos);
        bool pop_front();
        bool pop_back();
        uint8_t pop_back_byte();
        int size();
        void save(const std::string &fpath);
        void load(const std::string &fpath);
};

/* T_in: 
 * * internal type doing computation. 
 * * default: uint64_t
 * T_out: 
 * * interface type for io.
 * * default: uint32_t for cxx, int32 for python
 */
template <typename T_in, typename T_out>
// according to paper: ARITHMETIC CODING FOR DATA COMPRESSION
class ArithmeticCodingEncoder {
    public:
        BitStream bit_stream;

        /* precision: 
         * * aka Code_value_bits
         * * internal precision of arithmetic coding
         * * following paper ARITHMETIC CODING FOR DATA COMPRESSION
         * * requires:
         * * f \le c - 2 && f + c \le p
         * * default: 32
         */
        ArithmeticCodingEncoder(const int &precision);

        /* sym:
         * * symbol to encode, start from 0, should statisfy 0 <= sym < sym_cnt (alphabet size)
         * cdf:
         * * cdf for symbol, should have sym_cnt + 1 bins, and pmf(sym) = cdf[sym + 1] - cdf[sym]
         * * the last element must be power of 2, which is 2 ** cdf_bits
         * cdf_bits:
         * * 2 ** cdf_bits == last element of cdf, always <= _frequency_bits
         */
        void encode(const T_out &sym, const T_out *cdf, const int &cdf_bits);

        /* call before the end of encoding */
        void flush();

    private:
        void _renormalize();
        T_in _precision;
        T_in _full_range;
        T_in _half_range;
        T_in _quarter_range;
        T_in _three_forth_range;
        T_in _max_total;
        T_in _low;
        T_in _high;
        T_in _pending_bits;
};

/* T_in: 
 * * internal type doing computation. 
 * * default: uint64_t
 * T_out: 
 * * interface type for io.
 * * default: uint32_t for cxx, int32 for python
 */
template <typename T_in, typename T_out>
// according to paper: ARITHMETIC CODING FOR DATA COMPRESSION
class ArithmeticCodingDecoder{
  public:
    BitStream bit_stream;

    /* precision: 
    * * aka Code_value_bits
    * * internal precision of arithmetic coding
    * * following paper ARITHMETIC CODING FOR DATA COMPRESSION
    * * requires:
    * * f \le c - 2 && f + c \le p
    * * default: 32
    * encode_bit_stream:
    * * the BitStream ro decode from encoder / read from file
    */
    ArithmeticCodingDecoder(const int &precision, const BitStream &encode_bit_stream);

    /* sym_cnt:
    * * sym_cnt is the alphabet size
    * cdf:
    * * cdf for symbol, should have sym_cnt + 1 bins, and pmf(sym) = cdf[sym + 1] - cdf[sym]
    * * the last element must be power of 2, which is 2 ** cdf_bits
    * cdf_bits:
    * * 2 ** cdf_bits == last element of cdf, always <= _frequency_bits
    */
    T_out decode(const int &sym_cnt, const T_out *cdf, const int &cdf_bits);

  private:
    void _renormalize();
    T_in _precision;
    T_in _full_range;
    T_in _half_range;
    T_in _quarter_range;
    T_in _three_forth_range;
    T_in _max_total;
    T_in _low;
    T_in _high;
    T_in _pending_bits;
    T_in _code;
};

} // namespace AC
} // namespace llvm

#endif // ARITHMETIC_CODING_H