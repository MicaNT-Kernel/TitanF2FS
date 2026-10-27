#include "titanf2fs/f2fs_engine.hpp"
#include <iostream>
#include <iomanip>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "TitanF2FS Consistency Checker & Scrubber v1.0.0\n";
        std::cout << "Usage: titanf2fs_check <target_device_or_image>\n";
        std::cout << "Example: titanf2fs_check \\\\.\\PhysicalDrive2\n";
        std::cout << "         titanf2fs_check flash_drive.img\n";
        return 1;
    }

    std::string target = argv[1];
    if (target == "--help" || target == "-h" || target == "/?") {
        std::cout << "TitanF2FS Consistency Checker & Scrubber v1.0.0\n";
        std::cout << "Usage: titanf2fs_check <target_device_or_image>\n";
        return 0;
    }

    std::cout << "=================================================\n";
    std::cout << "           TitanF2FS Volume Inspection           \n";
    std::cout << "=================================================\n";
    std::cout << "Target Path:        " << target << "\n";

    titanf2fs::F2fsEngine engine;
    if (!engine.Open(target, true)) {
        std::cerr << "[-] Error: Failed to open F2FS filesystem at: " << target << "\n";
        return 1;
    }

    std::cout << "Volume Label:       " << engine.VolumeLabel() << "\n";
    std::cout << "Filesystem Magic:   0x" << std::hex << engine.SuperBlock().magic << std::dec << "\n";
    std::cout << "Block Size:         " << engine.BlockSize() << " bytes\n";
    std::cout << "Total Blocks:       " << engine.TotalBlocks() << "\n";
    std::cout << "Total Segments:     " << engine.SuperBlock().segment_count << "\n";
    std::cout << "Main Segments:      " << engine.SuperBlock().segment_count_main << "\n";
    std::cout << "Root Inode:         " << engine.RootInode() << "\n";
    std::cout << "Checkpoint Version: 0x" << std::hex << engine.Checkpoint().checkpoint_ver << std::dec << "\n";

    std::cout << "\n--- Performing Integrity & Log-Structured Scrub ---\n";
    titanf2fs::F2fsScrubReport report{};
    if (!engine.CheckAndScrub(report)) {
        std::cerr << "[-] Scrub failed with fatal read error.\n";
        return 1;
    }

    std::cout << "Blocks Scanned:     " << report.scannedBlocks << "\n";
    std::cout << "Inodes Scanned:     " << report.scannedInodes << "\n";
    std::cout << "Errors Detected:    " << report.errorsDetected << "\n";

    if (report.isClean && report.errorsDetected == 0) {
        std::cout << "[+] STATUS: VOLUME IS HEALTHY AND CONSISTENT\n";
        return 0;
    } else {
        std::cout << "[-] STATUS: CORRUPTION OR INCONSISTENCY DETECTED\n";
        return 2;
    }
}
