#include "titanf2fs/f2fs_engine.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <cctype>

void PrintUsage() {
    std::cout << "TitanF2FS Format Utility v1.0.0 (Clean-Room Samsung F2FS Engine for Windows NT)\n";
    std::cout << "Usage: titanf2fs_format <target_device_or_file> [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --size <MB>     Volume size in Megabytes (default: 64 MB)\n";
    std::cout << "  --label <name>  Filesystem volume label (default: TitanF2FS)\n";
    std::cout << "  --help          Display this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string target = argv[1];
    if (target == "--help" || target == "-h" || target == "/?") {
        PrintUsage();
        return 0;
    }

    uint64_t sizeMb = 64;
    std::string label = "TitanF2FS";

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            sizeMb = std::strtoull(argv[++i], nullptr, 10);
            if (sizeMb < 38) sizeMb = 38;
        } else if (arg == "--label" && i + 1 < argc) {
            label = argv[++i];
        } else if (arg.rfind("--", 0) != 0) {
            // Positional fallback
            if (i == 2 && std::isdigit(static_cast<unsigned char>(arg[0]))) {
                sizeMb = std::strtoull(arg.c_str(), nullptr, 10);
                if (sizeMb < 38) sizeMb = 38;
            } else if (i == 3 || (i == 2 && !std::isdigit(static_cast<unsigned char>(arg[0])))) {
                label = arg;
            }
        }
    }

    std::cout << "[+] Formatting target: " << target << "\n";
    std::cout << "[+] Target size: " << sizeMb << " MB\n";
    std::cout << "[+] Volume label: " << label << "\n";

    uint64_t sizeBytes = sizeMb * 1024ULL * 1024ULL;
    if (!titanf2fs::F2fsEngine::FormatVolume(target, sizeBytes, label)) {
        std::cerr << "[-] Error: Failed to format F2FS volume on target: " << target << "\n";
        return 1;
    }

    std::cout << "[+] F2FS filesystem successfully initialized!\n";
    std::cout << "[+] Superblock magic: 0xF2F52010 (Flash-Friendly File System)\n";
    std::cout << "[+] Segment size: 2 MB (512 blocks)\n";
    std::cout << "[+] Root directory: Inode 3\n";
    return 0;
}
