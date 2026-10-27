#pragma once

#include <cstdint>
#include <span>
#include <array>

namespace titanf2fs {

namespace detail {
constexpr std::array<uint32_t, 256> GenerateCrc32cTable() noexcept {
    constexpr uint32_t polynomial = 0x82F63B78; // Castagnoli polynomial
    std::array<uint32_t, 256> tbl{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (size_t j = 0; j < 8; ++j) {
            c = (c & 1) ? (polynomial ^ (c >> 1)) : (c >> 1);
        }
        tbl[i] = c;
    }
    return tbl;
}
inline constexpr auto Crc32cTable = GenerateCrc32cTable();

constexpr std::array<uint32_t, 256> GenerateCrc32Table() noexcept {
    constexpr uint32_t polynomial = 0xEDB88320; // IEEE 802.3 polynomial used by F2FS
    std::array<uint32_t, 256> tbl{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (size_t j = 0; j < 8; ++j) {
            c = (c & 1) ? (polynomial ^ (c >> 1)) : (c >> 1);
        }
        tbl[i] = c;
    }
    return tbl;
}
inline constexpr auto Crc32Table = GenerateCrc32Table();
} // namespace detail

class Crc32c {
public:
    static uint32_t Calculate(std::span<const uint8_t> data, uint32_t initial = 0xFFFFFFFF) noexcept {
        uint32_t crc = initial;
        for (uint8_t byte : data) {
            crc = detail::Crc32cTable[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    }
};

class F2fsCrc32 {
public:
    static uint32_t Calculate(std::span<const uint8_t> data, uint32_t initial = 0xF2F52010U) noexcept {
        uint32_t crc = initial;
        for (uint8_t byte : data) {
            crc = detail::Crc32Table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return crc;
    }
};

} // namespace titanf2fs
