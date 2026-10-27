#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace titanf2fs {

// ============================================================================
// Core Constants and Magic Numbers (F2FS is Little-Endian on disk)
// ============================================================================
constexpr uint32_t F2FS_SUPER_MAGIC          = 0xF2F52010U;
constexpr uint32_t F2FS_BLKSIZE              = 4096U;
constexpr uint32_t F2FS_SUPER_OFFSET         = 1024U;  // 1 KB (Sector 2)
constexpr uint32_t F2FS_SUPER_OFFSET2        = 5120U;  // 5 KB (SB Backup)
constexpr uint32_t F2FS_DEFAULT_SEGMENT_SIZE = 2 * 1024 * 1024U; // 2 MB
constexpr uint32_t F2FS_BLOCKS_PER_SEGMENT   = 512U;   // 2MB / 4KB

constexpr uint32_t F2FS_ROOT_INO             = 3U;     // Root directory inode
constexpr uint32_t F2FS_NODE_INO             = 1U;     // Node inode
constexpr uint32_t F2FS_META_INO             = 2U;     // Meta inode

constexpr uint32_t NR_DENTRY_IN_BLOCK        = 214U;
constexpr uint32_t F2FS_SLOT_LEN             = 8U;
constexpr uint32_t SIZE_OF_DENTRY_BITMAP     = 27U;
constexpr uint32_t SIZE_OF_DIR_ENTRY         = 11U;
constexpr uint32_t SIZE_OF_RESERVED          = 3U;     // 4096 - (27 + 214*11 + 214*8) = 3
constexpr uint32_t DEF_ADDRS_PER_INODE       = 923U;
constexpr uint32_t F2FS_NAME_LEN             = 255U;

// Inline flags (i_inline)
constexpr uint8_t F2FS_INLINE_XATTR          = 0x01;
constexpr uint8_t F2FS_INLINE_DATA           = 0x02;
constexpr uint8_t F2FS_INLINE_DENTRY         = 0x04;
constexpr uint8_t F2FS_DATA_EXIST            = 0x08;
constexpr uint8_t F2FS_INLINE_DOTS           = 0x10;

// Directory entry file types
constexpr uint8_t F2FS_FT_UNKNOWN            = 0;
constexpr uint8_t F2FS_FT_REG_FILE           = 1;
constexpr uint8_t F2FS_FT_DIR                = 2;
constexpr uint8_t F2FS_FT_CHRDEV             = 3;
constexpr uint8_t F2FS_FT_BLKDEV             = 4;
constexpr uint8_t F2FS_FT_FIFO               = 5;
constexpr uint8_t F2FS_FT_SOCK               = 6;
constexpr uint8_t F2FS_FT_SYMLINK            = 7;

// Inode file modes (POSIX octal)
constexpr uint16_t F2FS_S_IFMT               = 0170000;
constexpr uint16_t F2FS_S_IFREG              = 0100000;
constexpr uint16_t F2FS_S_IFDIR              = 0040000;
constexpr uint16_t F2FS_S_IFLNK              = 0120000;

// Checkpoint flags
constexpr uint32_t CP_UMOUNT_FLAG            = 0x00000001U;
constexpr uint32_t CP_COMPACT_SUM_FLAG       = 0x00000004U;
constexpr uint32_t CP_NAT_BITS_FLAG          = 0x00000080U;
constexpr uint32_t CP_TRIMMED_FLAG           = 0x00000100U;

#pragma pack(push, 1)

// ============================================================================
// F2FS Superblock (struct f2fs_super_block) - 3072 bytes at offset 1024 & 5120
// ============================================================================
struct F2fsSuperBlock {
    uint32_t magic;                    // 0x00: 0xF2F52010
    uint16_t major_ver;                // 0x04: Major version (1)
    uint16_t minor_ver;                // 0x06: Minor version (16)
    uint32_t log_sectorsize;           // 0x08: log2(512) = 9
    uint32_t log_sectors_per_block;    // 0x0C: log2(8) = 3
    uint32_t log_blocksize;            // 0x10: log2(4096) = 12
    uint32_t log_blocks_per_seg;       // 0x14: log2(512) = 9
    uint32_t segs_per_sec;             // 0x18: Segments per section (1)
    uint32_t secs_per_zone;            // 0x1C: Sections per zone (1)
    uint32_t checksum_offset;          // 0x20: Checksum offset
    uint64_t block_count;              // 0x24: Total block count
    uint32_t section_count;            // 0x2C: Total sections
    uint32_t segment_count;            // 0x30: Total segments
    uint32_t segment_count_ckpt;       // 0x34: Segments for checkpoint (2)
    uint32_t segment_count_sit;        // 0x38: Segments for SIT
    uint32_t segment_count_nat;        // 0x3C: Segments for NAT
    uint32_t segment_count_ssa;        // 0x40: Segments for SSA
    uint32_t segment_count_main;       // 0x44: Segments for main area
    uint32_t segment0_blkaddr;         // 0x48: Segment 0 start block address (512)
    uint32_t cp_blkaddr;               // 0x4C: Checkpoint block address (512)
    uint32_t sit_blkaddr;              // 0x50: SIT block address
    uint32_t nat_blkaddr;              // 0x54: NAT block address
    uint32_t ssa_blkaddr;              // 0x58: SSA block address
    uint32_t main_blkaddr;             // 0x5C: Main area start block address
    uint32_t root_ino;                 // 0x60: Root inode number (3)
    uint32_t node_ino;                 // 0x64: Node inode number (1)
    uint32_t meta_ino;                 // 0x68: Meta inode number (2)
    uint8_t  uuid[16];                 // 0x6C: 128-bit volume UUID
    uint16_t volume_name[512];         // 0x7C: UTF-16 volume label
    uint32_t extension_count;          // 0x47C: Extension count
    uint8_t  extension_list[64][8];    // 0x480: Extension strings
    uint32_t cp_payload;               // 0x680: Checkpoint payload blocks
    uint8_t  version[256];             // 0x684: Kernel version
    uint8_t  init_version[256];        // 0x784: Initial version
    uint32_t feature;                  // 0x884 (2180): Feature bits
    uint8_t  encryption_level;         // 0x888 (2184): Encryption level
    uint8_t  encrypt_pw_salt[16];      // 0x889 (2185): Salt
    uint8_t  devs_and_quotas[609];     // 0x899 (2201): Devices, quotas, and error status
    uint8_t  reserved[258];            // 0xAFA (2810): Reserved region
    uint32_t crc;                      // 0xBFC (3068): Superblock CRC32C
};
static_assert(sizeof(F2fsSuperBlock) == 3072, "F2fsSuperBlock must be exactly 3072 bytes");

// ============================================================================
// F2FS Checkpoint (struct f2fs_checkpoint) - 192 bytes base
// ============================================================================
struct F2fsCheckpoint {
    uint64_t checkpoint_ver;           // 0x00 (0): Checkpoint version
    uint64_t user_block_count;         // 0x08 (8): Total user blocks
    uint64_t valid_block_count;        // 0x10 (16): Valid blocks allocated
    uint32_t rsvd_segment_count;       // 0x18 (24): Reserved segments
    uint32_t overprov_segment_count;   // 0x1C (28): Overprovision segments
    uint32_t free_segment_count;       // 0x20 (32): Free segments in main area
    uint32_t cur_node_segno[8];        // 0x24 (36): Current node segments (8)
    uint16_t cur_node_blkoff[8];       // 0x44 (68): Current node block offsets (8)
    uint32_t cur_data_segno[8];        // 0x54 (84): Current data segments (8)
    uint16_t cur_data_blkoff[8];       // 0x74 (116): Current data block offsets (8)
    uint32_t ckpt_flags;               // 0x84 (132): Checkpoint flags (e.g. 0x185)
    uint32_t cp_pack_total_block_count;// 0x88 (136): CP pack total blocks
    uint32_t cp_pack_start_sum;        // 0x8C (140): Start block of data summary
    uint32_t valid_node_count;         // 0x90 (144): Valid node count
    uint32_t valid_inode_count;        // 0x94 (148): Valid inode count
    uint32_t next_free_nid;            // 0x98 (152): Next free node ID
    uint32_t sit_ver_bitmap_bytesize;  // 0x9C (156): SIT version bitmap size (64)
    uint32_t nat_ver_bitmap_bytesize;  // 0xA0 (160): NAT version bitmap size (64 or 256)
    uint32_t checksum_offset;          // 0xA4 (164): Checksum offset (0xFFC)
    uint64_t elapsed_time;             // 0xA8 (168): Elapsed time
    uint8_t  alloc_type[16];           // 0xB0 (176): Allocation types (16)
    uint8_t  sit_nat_version_bitmap[64];//0xC0 (192): Version bitmaps
};
static_assert(sizeof(F2fsCheckpoint) == 256, "F2fsCheckpoint header size check");

// ============================================================================
// Directory Entry (struct f2fs_dir_entry) - 11 bytes
// ============================================================================
struct F2fsDirEntry {
    uint32_t hash_code;                // Hash code of filename
    uint32_t ino;                      // Inode number
    uint16_t name_len;                 // Length of filename
    uint8_t  file_type;                // File type (F2FS_FT_REG_FILE, F2FS_FT_DIR)
};
static_assert(sizeof(F2fsDirEntry) == 11, "F2fsDirEntry must be exactly 11 bytes");

// ============================================================================
// Directory Block (struct f2fs_dentry_block) - 4096 bytes
// ============================================================================
struct F2fsDentryBlock {
    uint8_t      dentry_bitmap[SIZE_OF_DENTRY_BITMAP]; // 27 bytes
    uint8_t      reserved[SIZE_OF_RESERVED];           // 3 bytes
    F2fsDirEntry dentry[NR_DENTRY_IN_BLOCK];           // 214 * 11 = 2354 bytes
    uint8_t      filename[NR_DENTRY_IN_BLOCK][F2FS_SLOT_LEN]; // 214 * 8 = 1712 bytes
};
static_assert(sizeof(F2fsDentryBlock) == 4096, "F2fsDentryBlock must be exactly 4096 bytes");

// ============================================================================
// Node Footer (struct node_footer) - 24 bytes at end of each 4KB f2fs_node
// ============================================================================
struct F2fsNodeFooter {
    uint32_t nid;                      // Node ID
    uint32_t ino;                      // Inode number of file
    uint32_t cp_ver;                   // Checkpoint version
    uint32_t next_blkaddr;             // Next block address
    uint32_t flag;                     // Node flags
    uint32_t padding;                  // 64-bit alignment padding
};

// ============================================================================
// F2FS Inode (struct f2fs_inode) - 4072 bytes
// ============================================================================
struct F2fsInode {
    uint16_t i_mode;                   // 0x00: File mode (octal 040755, 0100644)
    uint8_t  i_advise;                 // 0x02: File hints
    uint8_t  i_inline;                 // 0x03: Inline flags
    uint32_t i_uid;                    // 0x04: User ID
    uint32_t i_gid;                    // 0x08: Group ID
    uint32_t i_links;                  // 0x0C: Hard links count
    uint64_t i_size;                   // 0x10: File size in bytes
    uint64_t i_blocks;                 // 0x18: Total blocks allocated
    uint64_t i_atime;                  // 0x20: Access time
    uint64_t i_ctime;                  // 0x28: Change time
    uint64_t i_mtime;                  // 0x30: Modification time
    uint32_t i_atime_nsec;             // 0x38: Access time nsec
    uint32_t i_ctime_nsec;             // 0x3C: Change time nsec
    uint32_t i_mtime_nsec;             // 0x40: Modification time nsec
    uint32_t i_generation;             // 0x44: File version
    uint32_t i_current_depth;          // 0x48: Directory depth
    uint32_t i_xattr_nid;              // 0x4C: Extended attributes node
    uint32_t i_flags;                  // 0x50: File attributes
    uint32_t i_pino;                   // 0x54: Parent inode number
    uint32_t i_namelen;                // 0x58: Filename length
    uint8_t  i_name[F2FS_NAME_LEN];    // 0x5C: Filename for SPOR (255 bytes)
    uint8_t  i_dir_level;              // 0x15B: Directory level
    uint8_t  i_ext[12];                // 0x15C: Cached largest extent
    uint32_t i_addr[DEF_ADDRS_PER_INODE];//0x168: Direct block addresses (923 * 4 = 3692 bytes)
    uint32_t i_nid[5];                 // Direct & Indirect node pointers (20 bytes)
};

// ============================================================================
// Full 4KB F2FS Node Block (struct f2fs_node) - 4096 bytes
// ============================================================================
struct F2fsNode {
    union {
        F2fsInode inode;
        uint8_t   raw[4096 - sizeof(F2fsNodeFooter)];
    };
    F2fsNodeFooter footer;
};
static_assert(sizeof(F2fsNode) == 4096, "F2fsNode must be exactly 4096 bytes");

// ============================================================================
// Node Address Table (NAT) Entry (struct f2fs_nat_entry) - 9 bytes
// ============================================================================
struct F2fsNatEntry {
    uint8_t  version;                  // Node version
    uint32_t ino;                      // Inode number
    uint32_t block_addr;               // Physical block address
};

#pragma pack(pop)

// ============================================================================
// In-Memory Representation Models
// ============================================================================
struct F2fsDirEntryInfo {
    uint32_t    inodeNumber{0};
    std::string name;
    uint8_t     fileType{F2FS_FT_REG_FILE};
};

struct F2fsScrubReport {
    uint64_t    totalBlocks{0};
    uint64_t    validBlocks{0};
    uint64_t    freeBlocks{0};
    uint32_t    totalSegments{0};
    uint32_t    validNodes{0};
    uint32_t    validInodes{0};
    uint64_t    scannedBlocks{0};
    uint64_t    scannedInodes{0};
    uint64_t    errorsDetected{0};
    std::string volumeLabel;
    bool        isClean{true};
};

} // namespace titanf2fs
