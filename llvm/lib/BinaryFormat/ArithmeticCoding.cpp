/*
Based on YAECL: Yet Another Entropy Coding Library for Neural Compression Research
by Tongda Xu, et al. (2022)
Under MIT License
*/
#include "llvm/BinaryFormat/ArithmeticCoding.h"
#include <cassert>
#include <bitset>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include <cstdint>

using namespace llvm::AC;

BitStream::BitStream() {
    _pos = 0;
    _fpos=0;
}

void BitStream::push_back(bool bit) {
    if(_pos % 8==0) _data.push_back(0);
    _data[_pos / 8] |= (bit << (7 - (_pos % 8)));
    _pos++;
}

void BitStream::push_back_byte(uint8_t byte) {
    // alined push for fast ANS
    assert(_pos % 8 == 0);
    _data.push_back(byte);
    _pos+=8;
}

bool BitStream::get(int pos) {
    assert(pos < _pos);
    return _data[pos / 8] & (1 << (7 - (pos % 8)));
}

bool BitStream::pop_front() {
    // queue style pop, use only with ac
    if (_fpos >= _pos) return 0;
    bool tmp = get(_fpos);
    _fpos++;
    return tmp;
}

bool BitStream::pop_back() {
    // queue style pop, use only with ans
    if (_pos <= 0) return 0;
    _pos--;
    return _data[_pos / 8] & (1 << (7 - (_pos % 8)));
}

uint8_t BitStream::pop_back_byte() {
    // alined pop for fast ANS
    assert(_pos % 8 == 0);
    _pos-=8;
    return _data[_pos / 8];
}

int BitStream::size() {
    return _pos;
}

void BitStream::save(const std::string &fpath) {
    std::ofstream of(fpath, std::ios::out | std::ios::binary);
    of.write(reinterpret_cast<const char*>(_data.data()), _pos / 8 + int(_pos % 8 != 0));
}

void BitStream::load(const std::string &fpath) {
    std::ifstream rf(fpath, std::ios::in | std::ios::binary);
    rf.seekg(0, rf.end);
    int flen = rf.tellg();
    rf.seekg(0, rf.beg);
    while(flen) {
        uint8_t tmp;
        rf.read(reinterpret_cast<char*>(&tmp), 1);
        _data.push_back(tmp);
        _pos+=8;
        flen--;
    }
}

template <typename T_in, typename T_out>
ArithmeticCodingEncoder<T_in,T_out>::ArithmeticCodingEncoder(const int &precision) {
    assert(precision >= 2 && precision < std::numeric_limits<decltype(_full_range)>::digits);
    _precision = precision;
    _full_range = (static_cast<decltype(_full_range)>(1) << _precision) - 1;
    _quarter_range = (_full_range >> 2) + 1;
    _half_range = _quarter_range * 2;
    _three_forth_range = _quarter_range * 3;
    int max_total_bits = std::min(_precision - 2, std::numeric_limits<decltype(_full_range)>::digits - _precision);
    _max_total = (static_cast<decltype(_full_range)>(1) << max_total_bits) - 1;
    _low = 0;
    _high = _full_range;
    _pending_bits = 0;
}

template <typename T_in, typename T_out>
void ArithmeticCodingEncoder<T_in,T_out>::encode(const T_out &sym, const T_out *cdf, const int &cdf_bits) {
    assert(_low < _high);
    assert((_low & _full_range) == _low);
    assert((_high & _full_range) == _high);
    T_in range = _high - _low + 1;
    T_in c_total = static_cast<decltype(c_total)>(1) << cdf_bits;
    assert(c_total <= _max_total);
    T_in c_low = cdf[sym];
    T_in c_high = cdf[sym + 1];
    assert(c_low != c_high);
    _high = _low + ((c_high * range) >> cdf_bits) - 1;
    _low  = _low + ((c_low  * range) >> cdf_bits);
    _renormalize();
}

template <typename T_in, typename T_out>
void ArithmeticCodingEncoder<T_in,T_out>::flush() {
    _pending_bits++;
    bool bit = static_cast<bool>(_low >= _quarter_range);
    bit_stream.push_back(bit);        
    for (; _pending_bits > 0; _pending_bits--)
        bit_stream.push_back(!bit);
}


template <typename T_in, typename T_out>
void ArithmeticCodingEncoder<T_in,T_out>::_renormalize() {
    while(1) {
        if(_high < _half_range) {
            bit_stream.push_back(0);        
            for (; _pending_bits > 0; _pending_bits--)
                bit_stream.push_back(1);
        } else if (_low >= _half_range) {
            bit_stream.push_back(1);        
            for (; _pending_bits > 0; _pending_bits--)
                bit_stream.push_back(0);
            _low -= _half_range;
            _high -= _half_range;
        }else if(_low>=_quarter_range&&_high<_three_forth_range) {
            assert(_pending_bits < std::numeric_limits<decltype(_pending_bits)>::max());
            _pending_bits++;
            _low-=_quarter_range;
            _high-=_quarter_range;
        }else {
            break;
        }
        _high = (_high << 1) + 1;
        _low <<=1;
    }
}

template <typename T_in, typename T_out>
ArithmeticCodingDecoder<T_in,T_out>::ArithmeticCodingDecoder(const int &precision, const BitStream &encode_bit_stream) {
    bit_stream = encode_bit_stream;
    assert(precision >= 2 && precision < std::numeric_limits<decltype(_full_range)>::digits);
    _precision = precision;
    _full_range = (static_cast<decltype(_full_range)>(1) << _precision) - 1;
    _quarter_range = (_full_range >> 2) + 1;
    _half_range = _quarter_range * 2;
    _three_forth_range = _quarter_range * 3;
    _max_total = (static_cast<decltype(_full_range)>(1) << (_precision - 2)) - 1;
    _max_total = std::min(_max_total, (static_cast<decltype(_full_range)>(1) << (std::numeric_limits<decltype(_full_range)>::digits - _precision - 1)) - 1);
    _low = 0;
    _high = _full_range;
    _pending_bits = 0;
    _code = 0;
    for(T_in i=0;i<_precision;i++) {
        _code <<= 1;
        _code += static_cast<int>(bit_stream.pop_front());
    }
}

template <typename T_in, typename T_out>
T_out ArithmeticCodingDecoder<T_in,T_out>::decode(const int &sym_cnt, const T_out *cdf, const int &cdf_bits) {
    T_in c_total = static_cast<decltype(c_total)>(1) << cdf_bits;
    assert(c_total <= _max_total);
    T_in range = _high - _low + 1;
    T_in scaled_range = _code - _low;
    T_in scaled_value = (((scaled_range + 1) << cdf_bits) - 1) / range;
    assert(scaled_value < c_total);
    T_in start = 0;
    T_in end = sym_cnt;
    while (end - start > 1) {
        T_in middle = (start + end) >> 1;
        if (cdf[middle] > scaled_value)
            end = middle;
        else
            start = middle;
    }
    assert(start + 1 == end);
    T_in sym = start;
    T_in c_low = cdf[sym];
    T_in c_high = cdf[sym + 1];
    assert(c_low != c_high);
    _high = _low + ((c_high * range) >> cdf_bits) - 1;
    _low  = _low + ((c_low  * range) >> cdf_bits);
    _renormalize();
    return static_cast<T_out>(sym);
}

template <typename T_in, typename T_out>
void ArithmeticCodingDecoder<T_in,T_out>::_renormalize() {
    while(1) {
        if(_high < _half_range) {
            // pass
        }else if(_low >= _half_range) {
            _code -= _half_range;
            _low -= _half_range;
            _high -= _half_range;
        }else if(_low >= _quarter_range && _high < _three_forth_range) {
            _code -= _quarter_range;
            _low -= _quarter_range;
            _high -= _quarter_range;
        }else{
            break;
        }
        _high = (_high << 1) + 1;
        _low <<=1;
        _code = (_code << 1) + static_cast<int>(bit_stream.pop_front());
    }
}

// Force template instantiation for linker
template class llvm::AC::ArithmeticCodingEncoder<uint64_t, uint32_t>;
template class llvm::AC::ArithmeticCodingDecoder<uint64_t, uint32_t>;