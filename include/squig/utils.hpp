#ifndef UTILS_H_
#define UTILS_H_

#include <cctype>   //std::isprint
#include <iomanip>  // std::hex
#include <iostream>
#include <vector>
namespace utils {
// inline: this function is defined in a header, so every .cpp that #includes
// it gets a copy. Without inline, the linker sees multiple definitions -> error
// (ODR violation). inline tells the linker "these are all the same, pick one."
// Note: class member functions defined in-class are implicitly inline
// (see PerfStatistics methods in perfstatistics.hpp).
inline uint64_t nowMs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(
               clock::now().time_since_epoch())
        .count();
}

inline void printHexDump(const std::vector<char>& buffer) {
    std::ios::fmtflags original_flags = std::cout.flags();
    char original_fill = std::cout.fill();

    const size_t bytesPerLine = 16;
    size_t length = buffer.size();

    for (size_t i = 0; i < length; i += bytesPerLine) {
        // 1. Print the Offset (e.g., 00000000)
        std::cout << std::hex << std::setw(8) << std::setfill('0') << i << ": ";

        // 2. Print the Hex Bytes
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < length) {
                // Cast to unsigned char first to avoid sign extension (e.g.,
                // ffffff80) Then cast to int so streams treat it as a number,
                // not a char
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                std::cout << std::setw(2) << std::setfill('0')
                          << static_cast<int>(byte) << " ";
            } else {
                // Padding for the last line to align the ASCII column
                std::cout << "   ";
            }
        }

        std::cout << " |";

        // 3. Print the ASCII Representation
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < length) {
                char c = buffer[i + j];
                // Check if character is printable; otherwise replace with '.'
                std::cout << (std::isprint(static_cast<unsigned char>(c))
                                  ? c
                                  : '.');
            }
        }
        std::cout << "|" << std::endl;
    }
    std::cout.flags(original_flags);
    std::cout.fill(original_fill);
}
}  // namespace utils

#endif  // UTILS_H_
