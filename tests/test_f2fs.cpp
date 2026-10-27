#include "titanf2fs/f2fs_engine.hpp"
#include "titanf2fs/crc32c.hpp"
#include "titanf2fs/compression.hpp"

#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } else { \
            std::cout << "[+] PASSED: " << msg << "\n"; \
        } \
    } while (0)

bool TestCrc32c() {
    std::cout << "\n=== TestCrc32c ===\n";
    const char* testStr = "123456789";
    uint32_t crc = titanf2fs::Crc32c::Calculate(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(testStr), 9)
    );
    TEST_ASSERT(crc == 0xE3069283U, "CRC32C standard test vector (123456789)");

    uint32_t emptyCrc = titanf2fs::Crc32c::Calculate(std::span<const uint8_t>{});
    TEST_ASSERT(emptyCrc == 0x00000000U, "CRC32C empty vector");
    return true;
}

bool TestCompression() {
    std::cout << "\n=== TestCompression ===\n";
    std::vector<uint8_t> sampleData;
    std::string text = "F2FS Flash-Friendly File System Clean-Room Storage Suite for Windows NT and MicaNT! ";
    for (int i = 0; i < 200; ++i) {
        sampleData.insert(sampleData.end(), text.begin(), text.end());
    }

    std::vector<uint8_t> compressed;
    TEST_ASSERT(titanf2fs::CompressionEngine::CompressLZ4(sampleData, compressed), "CompressLZ4 succeeded");
    TEST_ASSERT(compressed.size() < sampleData.size(), "LZ4 compressed size smaller than source");

    std::vector<uint8_t> decompressed;
    TEST_ASSERT(titanf2fs::CompressionEngine::DecompressLZ4(compressed, decompressed), "LZ4 decompression succeeded");
    TEST_ASSERT(decompressed == sampleData, "LZ4 decompressed matches original");
    return true;
}

bool TestF2fsFormatAndMount() {
    std::cout << "\n=== TestF2fsFormatAndMount ===\n";
    std::string testPath = "test_f2fs_unit.img";
    uint64_t testSize = 64ULL * 1024 * 1024; // 64 MB
    std::string testLabel = "TitanF2FSTest";

    if (fs::exists(testPath)) {
        fs::remove(testPath);
    }

    TEST_ASSERT(titanf2fs::F2fsEngine::FormatVolume(testPath, testSize, testLabel), "FormatVolume succeeded");
    TEST_ASSERT(fs::exists(testPath), "Image file created on disk");

    titanf2fs::F2fsEngine engine;
    TEST_ASSERT(engine.Open(testPath, true), "Engine Open succeeded");
    TEST_ASSERT(engine.IsOpen(), "Engine reports open state");
    TEST_ASSERT(engine.BlockSize() == 4096, "Block size is 4096 bytes");
    TEST_ASSERT(engine.VolumeLabel() == testLabel, "Volume label matches");
    TEST_ASSERT(engine.SuperBlock().magic == titanf2fs::F2FS_SUPER_MAGIC, "Superblock magic is 0xF2F52010");
    TEST_ASSERT(engine.RootInode() == 3, "Root inode is Inode 3");

    titanf2fs::F2fsNode rootNode{};
    TEST_ASSERT(engine.ReadNode(3, rootNode), "ReadNode 3 (Root) succeeded");
    TEST_ASSERT((rootNode.inode.i_mode & titanf2fs::F2FS_S_IFMT) == titanf2fs::F2FS_S_IFDIR, "Root inode is a directory");

    std::vector<titanf2fs::F2fsDirEntryInfo> entries;
    TEST_ASSERT(engine.ReadDirectory(3, entries), "ReadDirectory succeeded on Root directory");
    TEST_ASSERT(entries.size() >= 2, "Directory has at least '.' and '..'");

    bool hasDot = false, hasDotDot = false;
    for (const auto& e : entries) {
        if (e.name == ".") hasDot = true;
        if (e.name == "..") hasDotDot = true;
    }
    TEST_ASSERT(hasDot && hasDotDot, "Root directory contains '.' and '..'");

    uint32_t resolvedIno = 0;
    TEST_ASSERT(engine.ResolvePath("/", resolvedIno), "ResolvePath '/' succeeded");
    TEST_ASSERT(resolvedIno == 3, "ResolvePath '/' returns Root Inode 3");

    titanf2fs::F2fsScrubReport report{};
    TEST_ASSERT(engine.CheckAndScrub(report), "CheckAndScrub completed successfully");
    TEST_ASSERT(report.isClean, "Scrubber reports volume is clean");
    TEST_ASSERT(report.errorsDetected == 0, "Zero errors detected during scrub");

    engine.Close();
    fs::remove(testPath);
    return true;
}

int main() {
    std::cout << "=================================================\n";
    std::cout << "        TitanF2FS Core Unit Test Battery         \n";
    std::cout << "=================================================\n";

    if (!TestCrc32c()) return 1;
    if (!TestCompression()) return 1;
    if (!TestF2fsFormatAndMount()) return 1;

    std::cout << "\n-------------------------------------------------\n";
    std::cout << " ALL UNIT TESTS COMPLETED SUCCESSFULLY (0 ERRORS)\n";
    std::cout << "-------------------------------------------------\n";
    return 0;
}
