#pragma once

#include "titanf2fs/f2fs_types.hpp"
#include "titanf2fs/crc32c.hpp"

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <cmath>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace titanf2fs {

class F2fsEngine {
public:
    F2fsEngine() = default;
    ~F2fsEngine() { Close(); }

    F2fsEngine(const F2fsEngine&) = delete;
    F2fsEngine& operator=(const F2fsEngine&) = delete;

    bool Open(const std::string& path, bool readOnly = true) {
        Close();
        path_ = path;
        readOnly_ = readOnly;

#ifdef _WIN32
        DWORD access = GENERIC_READ;
        if (!readOnly) access |= GENERIC_WRITE;
        DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;

        hFile_ = CreateFileA(
            path.c_str(),
            access,
            share,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
            nullptr
        );

        if (hFile_ == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER liSize{};
        if (GetFileSizeEx(hFile_, &liSize)) {
            totalBytes_ = static_cast<uint64_t>(liSize.QuadPart);
        }
#else
        auto mode = std::ios::binary | std::ios::in;
        if (!readOnly) mode |= std::ios::out;
        fileStream_.open(path, mode);
        if (!fileStream_.is_open()) return false;

        fileStream_.seekg(0, std::ios::end);
        totalBytes_ = static_cast<uint64_t>(fileStream_.tellg());
        fileStream_.seekg(0, std::ios::beg);
#endif

        return LoadSuperBlock();
    }

    void Close() {
#ifdef _WIN32
        if (hFile_ != INVALID_HANDLE_VALUE && hFile_ != nullptr) {
            CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
#endif
        isOpen_ = false;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return isOpen_; }
    [[nodiscard]] uint32_t BlockSize() const noexcept { return blockSize_; }
    [[nodiscard]] uint64_t TotalBlocks() const noexcept { return totalBlocks_; }
    [[nodiscard]] uint64_t TotalBytes() const noexcept { return totalBytes_; }
    [[nodiscard]] const std::string& VolumeLabel() const noexcept { return label_; }
    [[nodiscard]] const F2fsSuperBlock& SuperBlock() const noexcept { return sb_; }
    [[nodiscard]] const F2fsCheckpoint& Checkpoint() const noexcept { return cp_; }
    [[nodiscard]] uint32_t RootInode() const noexcept { return rootIno_; }

    bool ReadBytes(uint64_t offset, size_t size, void* outBuf) {
        if (!outBuf || size == 0) return true;

#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN)) {
            return false;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(hFile_, outBuf, static_cast<DWORD>(size), &bytesRead, nullptr) || bytesRead != size) {
            return false;
        }
        return true;
#else
        fileStream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        fileStream_.read(reinterpret_cast<char*>(outBuf), size);
        return fileStream_.gcount() == static_cast<std::streamsize>(size);
#endif
    }

    bool WriteBytes(uint64_t offset, size_t size, const void* inBuf) {
        if (readOnly_ || !inBuf || size == 0) return false;

#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN)) {
            return false;
        }

        DWORD written = 0;
        if (!WriteFile(hFile_, inBuf, static_cast<DWORD>(size), &written, nullptr) || written != size) {
            return false;
        }
        return true;
#else
        fileStream_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        fileStream_.write(reinterpret_cast<const char*>(inBuf), size);
        return fileStream_.good();
#endif
    }

    bool ReadBlock(uint32_t blkAddr, void* outBuf) {
        uint64_t byteOffset = static_cast<uint64_t>(blkAddr) * blockSize_;
        return ReadBytes(byteOffset, blockSize_, outBuf);
    }

    bool ResolveNodeBlock(uint32_t nid, uint32_t& outBlkAddr) {
        if (nid == 0) return false;

        // In F2FS, Node 3 is the root directory
        if (nid == rootIno_) {
            // Check NAT entry first
            uint64_t natOffset = static_cast<uint64_t>(sb_.nat_blkaddr) * blockSize_ + (nid * sizeof(F2fsNatEntry));
            F2fsNatEntry natEntry{};
            if (ReadBytes(natOffset, sizeof(F2fsNatEntry), &natEntry) && natEntry.block_addr != 0) {
                outBlkAddr = natEntry.block_addr;
                return true;
            }
            // Fallback to start of main area
            outBlkAddr = sb_.main_blkaddr;
            return true;
        }

        // General NAT lookup
        uint64_t natOffset = static_cast<uint64_t>(sb_.nat_blkaddr) * blockSize_ + (nid * sizeof(F2fsNatEntry));
        F2fsNatEntry natEntry{};
        if (ReadBytes(natOffset, sizeof(F2fsNatEntry), &natEntry) && natEntry.block_addr != 0) {
            outBlkAddr = natEntry.block_addr;
            return true;
        }

        return false;
    }

    bool ReadNode(uint32_t nid, F2fsNode& outNode) {
        uint32_t blkAddr = 0;
        if (!ResolveNodeBlock(nid, blkAddr)) {
            return false;
        }
        return ReadBlock(blkAddr, &outNode);
    }

    bool ReadDirectory(uint32_t dirIno, std::vector<F2fsDirEntryInfo>& outEntries) {
        outEntries.clear();
        F2fsNode node{};
        if (!ReadNode(dirIno, node)) {
            return false;
        }

        if ((node.inode.i_mode & F2FS_S_IFMT) != F2FS_S_IFDIR) {
            return false;
        }

        outEntries.push_back({dirIno, ".", F2FS_FT_DIR});
        uint32_t pino = node.inode.i_pino != 0 ? node.inode.i_pino : dirIno;
        outEntries.push_back({pino, "..", F2FS_FT_DIR});

        // 1. Check direct dentry blocks
        for (uint32_t b = 0; b < 10; ++b) {
            uint32_t blkAddr = node.inode.i_addr[b];
            if (blkAddr == 0 || blkAddr == 0xFFFFFFFFU) continue;

            F2fsDentryBlock dblk{};
            if (!ReadBlock(blkAddr, &dblk)) continue;

            for (uint32_t i = 0; i < NR_DENTRY_IN_BLOCK; ++i) {
                uint32_t byteIdx = i / 8;
                uint8_t bitMask = 1 << (i % 8);
                if (byteIdx < SIZE_OF_DENTRY_BITMAP && (dblk.dentry_bitmap[byteIdx] & bitMask)) {
                    const auto& de = dblk.dentry[i];
                    uint16_t nameLen = de.name_len;
                    if (nameLen > 0 && nameLen <= F2FS_NAME_LEN && de.ino != 0) {
                        std::string name;
                        uint32_t slotsNeeded = (nameLen + F2FS_SLOT_LEN - 1) / F2FS_SLOT_LEN;
                        for (uint32_t s = 0; s < slotsNeeded && (i + s) < NR_DENTRY_IN_BLOCK; ++s) {
                            size_t copyBytes = std::min(size_t(F2FS_SLOT_LEN), size_t(nameLen - name.size()));
                            name.append(reinterpret_cast<const char*>(dblk.filename[i + s]), copyBytes);
                        }
                        if (name != "." && name != "..") {
                            outEntries.push_back({de.ino, std::move(name), de.file_type});
                        }
                        if (slotsNeeded > 1) {
                            i += slotsNeeded - 1;
                        }
                    }
                }
            }
        }

        return true;
    }

    bool ReadFileData(const F2fsInode& inode, uint64_t fileOffset, size_t size, std::vector<uint8_t>& outData) {
        outData.clear();
        uint64_t fileSize = inode.i_size;
        if (fileOffset >= fileSize) return true;

        size_t bytesToRead = std::min(static_cast<uint64_t>(size), fileSize - fileOffset);
        outData.resize(bytesToRead);

        size_t bytesDone = 0;
        uint64_t curOffset = fileOffset;

        while (bytesDone < bytesToRead) {
            uint32_t logicalBlock = static_cast<uint32_t>(curOffset / blockSize_);
            uint32_t blockOffset = static_cast<uint32_t>(curOffset % blockSize_);
            uint32_t chunk = std::min(blockSize_ - blockOffset, static_cast<uint32_t>(bytesToRead - bytesDone));

            uint32_t physicalBlock = (logicalBlock < DEF_ADDRS_PER_INODE) ? inode.i_addr[logicalBlock] : 0;
            if (physicalBlock != 0 && physicalBlock != 0xFFFFFFFFU) {
                uint64_t diskOffset = (static_cast<uint64_t>(physicalBlock) * blockSize_) + blockOffset;
                if (!ReadBytes(diskOffset, chunk, outData.data() + bytesDone)) {
                    return false;
                }
            } else {
                std::memset(outData.data() + bytesDone, 0, chunk);
            }

            bytesDone += chunk;
            curOffset += chunk;
        }

        return true;
    }

    bool ResolvePath(const std::string& path, uint32_t& outIno) {
        if (!isOpen_) return false;
        if (path.empty() || path == "/" || path == "\\") {
            outIno = rootIno_;
            return true;
        }

        std::vector<std::string> parts;
        std::string cur;
        for (char c : path) {
            if (c == '/' || c == '\\') {
                if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) parts.push_back(cur);

        uint32_t currentIno = rootIno_;
        for (const auto& part : parts) {
            std::vector<F2fsDirEntryInfo> entries;
            if (!ReadDirectory(currentIno, entries)) return false;

            bool found = false;
            for (const auto& e : entries) {
                if (e.name == part) {
                    currentIno = e.inodeNumber;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        outIno = currentIno;
        return true;
    }

    bool CheckAndScrub(F2fsScrubReport& report) {
        report = F2fsScrubReport{};
        if (!isOpen_) return false;

        report.totalBlocks = totalBlocks_;
        report.validBlocks = cp_.valid_block_count;
        report.freeBlocks = static_cast<uint64_t>(cp_.free_segment_count) * F2FS_BLOCKS_PER_SEGMENT;
        report.totalSegments = sb_.segment_count;
        report.validNodes = cp_.valid_node_count;
        report.validInodes = cp_.valid_inode_count;
        report.volumeLabel = label_;

        // 1. Verify Superblock Magic
        if (sb_.magic != F2FS_SUPER_MAGIC) {
            report.errorsDetected++;
            report.isClean = false;
            return false;
        }

        // 2. Verify Superblock Geometry Checks
        if (sb_.log_blocksize != 12 || sb_.log_sectorsize != 9) {
            report.errorsDetected++;
        }

        // 3. Inspect Checkpoint Block
        if (cp_.user_block_count == 0 || cp_.valid_block_count > totalBlocks_) {
            report.errorsDetected++;
        }

        // 4. Verify Root Inode 3
        F2fsNode rootNode{};
        if (!ReadNode(rootIno_, rootNode)) {
            report.errorsDetected++;
            report.isClean = false;
        } else {
            if ((rootNode.inode.i_mode & F2FS_S_IFMT) != F2FS_S_IFDIR) {
                report.errorsDetected++;
            }
        }

        // 5. Directory Traversal & Bit-rot Scan
        std::vector<F2fsDirEntryInfo> rootEntries;
        if (ReadDirectory(rootIno_, rootEntries)) {
            report.scannedInodes += rootEntries.size();
            for (const auto& e : rootEntries) {
                if (e.name == "." || e.name == "..") continue;
                F2fsNode childNode{};
                if (!ReadNode(e.inodeNumber, childNode)) {
                    report.errorsDetected++;
                } else {
                    report.scannedBlocks += childNode.inode.i_blocks;
                }
            }
        }

        report.isClean = (report.errorsDetected == 0);
        return true;
    }

    static bool FormatVolume(const std::string& path, uint64_t sizeBytes, const std::string& label = "TitanF2FS") {
        constexpr uint32_t BLK_SIZE = F2FS_BLKSIZE;
        constexpr uint32_t BLKS_PER_SEG = F2FS_BLOCKS_PER_SEGMENT; // 512 blocks = 2 MB
        constexpr uint32_t SECTOR_SIZE = 512;

        if (sizeBytes < 38ULL * 1024 * 1024) { // Minimum ~38 MB
            sizeBytes = 38ULL * 1024 * 1024;
        }

        uint64_t totalBlocks = sizeBytes / BLK_SIZE;
        uint32_t totalSegments = static_cast<uint32_t>(totalBlocks / BLKS_PER_SEG);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;

        std::vector<uint8_t> zeroBlock(BLK_SIZE, 0);

        // Segment Allocation Layout:
        // Segment 0: Checkpoint Pack (blocks 512..1535 = 2 segments)
        // Segment 2: SIT (blocks 1536..2559 = 2 segments)
        // Segment 4: NAT (blocks 2560..3583 = 2 segments)
        // Segment 6: SSA (blocks 3584..4095 = 1 segment)
        // Segment 7..N: Main Area (starts at block 4096)
        uint32_t segment0Blk = 512;
        uint32_t cpBlk = 512;
        uint32_t sitBlk = 1536;
        uint32_t natBlk = 2560;
        uint32_t ssaBlk = 3584;
        uint32_t mainBlk = 4096;

        uint32_t mainSegments = (totalSegments > 8) ? (totalSegments - 8) : 16;
        uint32_t freeSegments = (mainSegments > 2) ? (mainSegments - 2) : 1;

        // 1. Superblock Setup (Offset 1024)
        F2fsSuperBlock sb{};
        sb.magic = F2FS_SUPER_MAGIC;
        sb.major_ver = 1;
        sb.minor_ver = 16;
        sb.log_sectorsize = 9;
        sb.log_sectors_per_block = 3;
        sb.log_blocksize = 12;
        sb.log_blocks_per_seg = 9;
        sb.segs_per_sec = 1;
        sb.secs_per_zone = 1;
        sb.checksum_offset = 0;
        sb.block_count = totalBlocks;
        sb.section_count = mainSegments;
        sb.segment_count = totalSegments;
        sb.segment_count_ckpt = 2;
        sb.segment_count_sit = 2;
        sb.segment_count_nat = 2;
        sb.segment_count_ssa = 1;
        sb.segment_count_main = mainSegments;
        sb.segment0_blkaddr = segment0Blk;
        sb.cp_blkaddr = cpBlk;
        sb.sit_blkaddr = sitBlk;
        sb.nat_blkaddr = natBlk;
        sb.ssa_blkaddr = ssaBlk;
        sb.main_blkaddr = mainBlk;
        sb.root_ino = F2FS_ROOT_INO;
        sb.node_ino = F2FS_NODE_INO;
        sb.meta_ino = F2FS_META_INO;

        for (int i = 0; i < 16; ++i) {
            sb.uuid[i] = static_cast<uint8_t>((std::rand() ^ (sizeBytes >> (i * 2))) & 0xFF);
        }

        // Copy label as UTF-16
        for (size_t i = 0; i < label.size() && i < 511; ++i) {
            sb.volume_name[i] = static_cast<uint16_t>(label[i]);
        }

        // 2. Checkpoint Setup
        F2fsCheckpoint cp{};
        cp.checkpoint_ver = 1;
        cp.user_block_count = static_cast<uint64_t>(mainSegments) * BLKS_PER_SEG;
        cp.valid_block_count = 2; // Root inode block + Root dentry block
        cp.rsvd_segment_count = 8;
        cp.overprov_segment_count = 10;
        cp.free_segment_count = freeSegments;
        cp.ckpt_flags = CP_UMOUNT_FLAG | CP_COMPACT_SUM_FLAG;
        cp.cp_pack_total_block_count = 6;
        cp.cp_pack_start_sum = 1;
        cp.valid_node_count = 1;  // Root inode (Node 3)
        cp.valid_inode_count = 1; // Root inode (Inode 3)
        cp.next_free_nid = 4;
        cp.sit_ver_bitmap_bytesize = 64;
        cp.nat_ver_bitmap_bytesize = 64;
        cp.checksum_offset = 0xFFC;

        // 3. Root Inode (Node 3) Setup
        F2fsNode rootNode{};
        rootNode.inode.i_mode = F2FS_S_IFDIR | 0755;
        rootNode.inode.i_links = 2;
        rootNode.inode.i_size = BLK_SIZE;
        rootNode.inode.i_blocks = 2;
        rootNode.inode.i_current_depth = 1;
        rootNode.inode.i_addr[0] = mainBlk + 1; // Data block 0 at block mainBlk + 1
        rootNode.footer.nid = F2FS_ROOT_INO;
        rootNode.footer.ino = F2FS_ROOT_INO;
        rootNode.footer.cp_ver = 1;

        // 4. Root Directory Dentry Block
        F2fsDentryBlock rootDblk{};
        // In F2FS, an empty root directory has 0 active entries in dentry_bitmap ('.' and '..' are virtual or handled in dentry)
        std::memset(rootDblk.dentry_bitmap, 0, sizeof(rootDblk.dentry_bitmap));

        // 5. NAT Entry for Root Inode (NID 3)
        F2fsNatEntry rootNat{};
        rootNat.version = 0;
        rootNat.ino = F2FS_ROOT_INO;
        rootNat.block_addr = mainBlk;

        // Write Blocks to Output:
        // Sector 0 & 1: 1024 bytes padding / boot code
        std::vector<uint8_t> preSb(F2FS_SUPER_OFFSET, 0);
        out.write(reinterpret_cast<const char*>(preSb.data()), F2FS_SUPER_OFFSET);

        // Superblock 0 (at offset 1024)
        out.write(reinterpret_cast<const char*>(&sb), sizeof(sb));

        // Pad to offset 5120 (4096 + 1024) for Superblock 1
        size_t currentPos = F2FS_SUPER_OFFSET + sizeof(sb);
        if (currentPos < F2FS_SUPER_OFFSET2) {
            std::vector<uint8_t> sbPad(F2FS_SUPER_OFFSET2 - currentPos, 0);
            out.write(reinterpret_cast<const char*>(sbPad.data()), sbPad.size());
        }

        // Superblock 1 (at offset 5120)
        out.write(reinterpret_cast<const char*>(&sb), sizeof(sb));

        // Pad to Segment 0 (block 512 = byte offset 512 * 4096 = 2,097,152)
        currentPos = F2FS_SUPER_OFFSET2 + sizeof(sb);
        uint64_t seg0Byte = static_cast<uint64_t>(segment0Blk) * BLK_SIZE;
        if (currentPos < seg0Byte) {
            uint64_t padLen = seg0Byte - currentPos;
            while (padLen > 0) {
                size_t chunk = std::min(padLen, static_cast<uint64_t>(BLK_SIZE));
                out.write(reinterpret_cast<const char*>(zeroBlock.data()), chunk);
                padLen -= chunk;
            }
        }

        // Prepare Checkpoint Block (4KB) with CRC at offset 4092 (0xFFC)
        std::vector<uint8_t> cpBlock(BLK_SIZE, 0);
        cp.checksum_offset = 4092;
        std::memcpy(cpBlock.data(), &cp, sizeof(cp));
        uint32_t cpCrc = F2fsCrc32::Calculate(std::span<const uint8_t>(cpBlock.data(), 4092), F2FS_SUPER_MAGIC);
        std::memcpy(cpBlock.data() + 4092, &cpCrc, 4);

        // CP Pack 0 (blocks 512..1023)
        // Block 512: Header CP
        out.write(reinterpret_cast<const char*>(cpBlock.data()), BLK_SIZE);
        // Blocks 513..516: Summary blocks (zeroed)
        for (int b = 1; b < 5; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }
        // Block 517: Footer CP
        out.write(reinterpret_cast<const char*>(cpBlock.data()), BLK_SIZE);
        // Blocks 518..1023: Remaining blocks of Segment 0
        for (uint32_t b = 6; b < BLKS_PER_SEG; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        // CP Pack 1 (blocks 1024..1535 = Segment 1)
        // Block 1024: Header CP
        out.write(reinterpret_cast<const char*>(cpBlock.data()), BLK_SIZE);
        // Blocks 1025..1028: Summary blocks
        for (int b = 1; b < 5; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }
        // Block 1029: Footer CP
        out.write(reinterpret_cast<const char*>(cpBlock.data()), BLK_SIZE);
        // Blocks 1030..1535: Remaining blocks of Segment 1
        for (uint32_t b = 6; b < BLKS_PER_SEG; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        // Blocks 1536..2559 (SIT Area - 2 segments = 1024 blocks)
        for (uint32_t b = 0; b < 2 * BLKS_PER_SEG; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        // Block 2560: NAT Area (Segment 4)
        std::vector<uint8_t> natBlock(BLK_SIZE, 0);
        std::memcpy(natBlock.data() + (F2FS_ROOT_INO * sizeof(F2fsNatEntry)), &rootNat, sizeof(rootNat));
        out.write(reinterpret_cast<const char*>(natBlock.data()), BLK_SIZE);
        // Blocks 2561..3583: Remaining NAT blocks (2 segments - 1 block)
        for (uint32_t b = 1; b < 2 * BLKS_PER_SEG; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        // Blocks 3584..4095: SSA Area (Segment 6 - 1 segment = 512 blocks)
        for (uint32_t b = 0; b < BLKS_PER_SEG; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        // Write Root Inode (Node 3) at block 4096
        out.write(reinterpret_cast<const char*>(&rootNode), sizeof(rootNode));

        // Write Root Dentry Block at block 4097
        out.write(reinterpret_cast<const char*>(&rootDblk), sizeof(rootDblk));

        // Pad remaining file to totalBlocks
        uint64_t writtenBlocks = mainBlk + 2;
        for (uint64_t b = writtenBlocks; b < totalBlocks; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        out.flush();
        return true;
    }

private:
    bool LoadSuperBlock() {
        // Read Superblock 0 (Offset 1024)
        if (!ReadBytes(F2FS_SUPER_OFFSET, sizeof(F2fsSuperBlock), &sb_)) {
            return false;
        }

        if (sb_.magic != F2FS_SUPER_MAGIC) {
            // Try Superblock 1 (Offset 5120)
            if (!ReadBytes(F2FS_SUPER_OFFSET2, sizeof(F2fsSuperBlock), &sb_)) {
                return false;
            }
            if (sb_.magic != F2FS_SUPER_MAGIC) {
                return false;
            }
        }

        blockSize_ = (sb_.log_blocksize >= 9 && sb_.log_blocksize <= 16) ? (1U << sb_.log_blocksize) : 4096;
        totalBlocks_ = sb_.block_count;
        rootIno_ = sb_.root_ino != 0 ? sb_.root_ino : F2FS_ROOT_INO;

        // Read Volume Label (UTF-16)
        std::wstring wLabel;
        for (int i = 0; i < 512 && sb_.volume_name[i] != 0; ++i) {
            wLabel.push_back(static_cast<wchar_t>(sb_.volume_name[i]));
        }
        label_ = std::string(wLabel.begin(), wLabel.end());
        if (label_.empty()) label_ = "TitanF2FS";

        // Read Checkpoint
        uint64_t cpOffset = static_cast<uint64_t>(sb_.cp_blkaddr) * blockSize_;
        ReadBytes(cpOffset, sizeof(F2fsCheckpoint), &cp_);

        isOpen_ = true;
        return true;
    }

    std::string path_;
    bool readOnly_{true};
    bool isOpen_{false};
    uint32_t blockSize_{4096};
    uint64_t totalBlocks_{0};
    uint64_t totalBytes_{0};
    uint32_t rootIno_{F2FS_ROOT_INO};
    std::string label_;
    F2fsSuperBlock sb_{};
    F2fsCheckpoint cp_{};

#ifdef _WIN32
    HANDLE hFile_{INVALID_HANDLE_VALUE};
#else
    std::fstream fileStream_;
#endif
};

} // namespace titanf2fs
