#ifndef UTILS_H_
#define UTILS_H_

#include <iostream>
#include <iomanip> // std::hex
#include <cctype> //std::isprint
namespace utils {
   void printHexDump(const std::vector<char>& buffer) {
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
                // Cast to unsigned char first to avoid sign extension (e.g., ffffff80)
                // Then cast to int so streams treat it as a number, not a char
                unsigned char byte = static_cast<unsigned char>(buffer[i + j]);
                std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
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
                std::cout << (std::isprint(static_cast<unsigned char>(c)) ? c : '.');
            }
        }
        std::cout << "|" << std::endl;
    }
    std::cout.flags(original_flags);
     std::cout.fill(original_fill);
}
}

#endif // UTILS_H_
