#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace titanf2fs {

class CompressionEngine {
public:
    static bool DecompressRLE(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst, size_t maxOut = 64 * 1024 * 1024) {
        dst.clear();
        if (src.empty()) return true;

        uint8_t flag = src[0];
        size_t i = 1;
        while (i < src.size()) {
            if (src[i] == flag) {
                if (i + 2 >= src.size()) return false;
                uint8_t count = src[i + 1];
                uint8_t byteVal = src[i + 2];
                i += 3;
                if (dst.size() + count > maxOut) return false;
                dst.insert(dst.end(), count, byteVal);
            } else {
                if (dst.size() + 1 > maxOut) return false;
                dst.push_back(src[i++]);
            }
        }
        return true;
    }

    static bool CompressLZ4(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
        dst.clear();
        if (src.empty()) return true;

        dst.resize(4);
        uint32_t origSize = static_cast<uint32_t>(src.size());
        std::memcpy(dst.data(), &origSize, 4);

        constexpr size_t HASH_LOG = 12;
        constexpr size_t HASH_SIZE = 1 << HASH_LOG;
        int32_t hashTable[HASH_SIZE];
        std::fill(std::begin(hashTable), std::end(hashTable), -1);

        auto hash4 = [](const uint8_t* p) -> uint32_t {
            uint32_t val;
            std::memcpy(&val, p, 4);
            return (val * 2654435761U) >> (32 - HASH_LOG);
        };

        size_t ip = 0;
        size_t anchor = 0;
        const size_t srcLen = src.size();
        const size_t matchLimit = srcLen - 5;

        while (ip < matchLimit) {
            uint32_t h = hash4(&src[ip]);
            int32_t ref = hashTable[h];
            hashTable[h] = static_cast<int32_t>(ip);

            if (ref >= 0 && (ip - ref) < 65535 &&
                src[ref] == src[ip] &&
                src[ref + 1] == src[ip + 1] &&
                src[ref + 2] == src[ip + 2] &&
                src[ref + 3] == src[ip + 3]) {

                size_t litLen = ip - anchor;
                size_t matchLen = 4;
                while (ip + matchLen < srcLen && src[ref + matchLen] == src[ip + matchLen]) {
                    matchLen++;
                }

                uint8_t token = static_cast<uint8_t>((std::min(litLen, size_t(15)) << 4) |
                                                     std::min(matchLen - 4, size_t(15)));
                dst.push_back(token);

                if (litLen >= 15) {
                    size_t rem = litLen - 15;
                    while (rem >= 255) { dst.push_back(255); rem -= 255; }
                    dst.push_back(static_cast<uint8_t>(rem));
                }

                dst.insert(dst.end(), src.begin() + anchor, src.begin() + anchor + litLen);

                uint16_t offset = static_cast<uint16_t>(ip - ref);
                dst.push_back(static_cast<uint8_t>(offset & 0xFF));
                dst.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));

                if (matchLen - 4 >= 15) {
                    size_t rem = (matchLen - 4) - 15;
                    while (rem >= 255) { dst.push_back(255); rem -= 255; }
                    dst.push_back(static_cast<uint8_t>(rem));
                }

                ip += matchLen;
                anchor = ip;
            } else {
                ip++;
            }
        }

        size_t litLen = srcLen - anchor;
        if (litLen > 0) {
            uint8_t token = static_cast<uint8_t>(std::min(litLen, size_t(15)) << 4);
            dst.push_back(token);
            if (litLen >= 15) {
                size_t rem = litLen - 15;
                while (rem >= 255) { dst.push_back(255); rem -= 255; }
                dst.push_back(static_cast<uint8_t>(rem));
            }
            dst.insert(dst.end(), src.begin() + anchor, src.end());
        }

        return true;
    }

    static bool DecompressLZ4(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
        dst.clear();
        if (src.size() < 4) return false;

        uint32_t expectedSize = 0;
        std::memcpy(&expectedSize, src.data(), 4);
        dst.reserve(expectedSize);

        size_t ip = 4;
        while (ip < src.size()) {
            uint8_t token = src[ip++];
            size_t litLen = (token >> 4) & 0x0F;
            if (litLen == 15) {
                while (ip < src.size() && src[ip] == 255) {
                    litLen += 255;
                    ip++;
                }
                if (ip >= src.size()) return false;
                litLen += src[ip++];
            }

            if (ip + litLen > src.size()) return false;
            dst.insert(dst.end(), src.begin() + ip, src.begin() + ip + litLen);
            ip += litLen;

            if (ip >= src.size()) break;

            if (ip + 2 > src.size()) return false;
            uint16_t offset = static_cast<uint16_t>(src[ip] | (src[ip + 1] << 8));
            ip += 2;
            if (offset == 0 || offset > dst.size()) return false;

            size_t matchLen = (token & 0x0F) + 4;
            if ((token & 0x0F) == 15) {
                while (ip < src.size() && src[ip] == 255) {
                    matchLen += 255;
                    ip++;
                }
                if (ip >= src.size()) return false;
                matchLen += src[ip++];
            }

            size_t copyFrom = dst.size() - offset;
            for (size_t k = 0; k < matchLen; ++k) {
                dst.push_back(dst[copyFrom + k]);
            }
        }

        return dst.size() == expectedSize;
    }
};

} // namespace titanf2fs
