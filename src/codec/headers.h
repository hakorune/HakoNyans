#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>

namespace hakonyans {

/**
 * .hkn FileHeader (48 bytes, fixed)
 */
struct FileHeader {
    uint32_t magic;         // 'HKN\0' = 0x484B4E00
    uint16_t version;       // 0x0002 (v0.2)
    uint16_t flags;         // bit0: 0=lossy, 1=lossless
    uint32_t width;         // 画像幅
    uint32_t height;        // 画像高
    uint8_t  bit_depth;     // 8, 10, 12, 16
    uint8_t  num_channels;  // 1=Gray, 3=YCbCr, 4=RGBA
    uint8_t  colorspace;    // 0=YCbCr, 1=YCoCg-R, 2=RGB
    uint8_t  subsampling;   // 0=4:4:4, 1=4:2:2, 2=4:2:0
    uint16_t tile_cols;     // タイル列数
    uint16_t tile_rows;     // タイル行数
    uint8_t  block_size;    // 8 (固定)
    uint8_t  transform_type;// 0=DCT-II
    uint8_t  entropy_type;  // 0=NyANS-P
    uint8_t  interleave_n;  // rANS 状態数 (8)
    uint8_t  pindex_density;// 0=none, 1=64KB, 2=16KB, 3=4KB
    uint8_t  quality;       // 1..100 (0=lossless)
    uint16_t _padding1;     // Alignment padding
    uint8_t  reserved[16];  // 将来拡張用
    
    static constexpr uint32_t MAGIC = 0x484B4E00;  // 'HKN\0'
    static constexpr uint16_t VERSION = 0x0002;    // v0.2
    
    FileHeader() {
        std::memset(this, 0, sizeof(FileHeader));
        magic = MAGIC;
        version = VERSION;
        block_size = 8;
        entropy_type = 0;  // NyANS-P
        interleave_n = 8;
    }
    
    /**
     * Validate header
     */
    bool is_valid() const {
        if (magic != MAGIC) return false;
        if (version != VERSION) return false;
        if (block_size != 8) return false;
        if (width == 0 || height == 0) return false;
        if (num_channels == 0 || num_channels > 4) return false;
        return true;
    }
    
    /**
     * Get padded dimensions (multiple of 8)
     */
    uint32_t padded_width() const {
        return ((width + 7) / 8) * 8;
    }
    
    uint32_t padded_height() const {
        return ((height + 7) / 8) * 8;
    }
    
    /**
     * Write to buffer
     */
    void write(uint8_t* buffer) const {
        std::memcpy(buffer, this, sizeof(FileHeader));
    }
    
    /**
     * Read from buffer
     */
    static FileHeader read(const uint8_t* buffer) {
        FileHeader header;
        std::memcpy(&header, buffer, sizeof(FileHeader));
        return header;
    }
};

static_assert(sizeof(FileHeader) == 48, "FileHeader must be 48 bytes");

/**
 * Chunk directory entry
 */
struct ChunkEntry {
    char type[4];       // ASCII type code (e.g., "QMAT", "TILE")
    uint64_t offset;    // Byte offset from file start
    uint64_t size;      // Chunk size in bytes
    
    ChunkEntry() : offset(0), size(0) {
        std::memset(type, 0, 4);
    }
    
    ChunkEntry(const char* t, uint64_t off, uint64_t sz)
        : offset(off), size(sz) {
        std::strncpy(type, t, 4);
    }
    
    std::string type_str() const {
        return std::string(type, 4);
    }
};

/**
 * Chunk directory
 */
class ChunkDirectory {
public:
    std::vector<ChunkEntry> entries;
    
    /**
     * Add chunk entry
     */
    void add(const char* type, uint64_t offset, uint64_t size) {
        entries.emplace_back(type, offset, size);
    }
    
    /**
     * Find chunk by type
     */
    const ChunkEntry* find(const char* type) const {
        for (const auto& entry : entries) {
            if (std::strncmp(entry.type, type, 4) == 0) {
                return &entry;
            }
        }
        return nullptr;
    }
    
    /**
     * Serialize to buffer
     */
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;
        uint32_t count = static_cast<uint32_t>(entries.size());
        
        // chunk_count (4 bytes)
        buffer.resize(4);
        std::memcpy(buffer.data(), &count, 4);
        
        // entries (20 bytes each)
        for (const auto& entry : entries) {
            size_t offset = buffer.size();
            buffer.resize(offset + 20);
            std::memcpy(&buffer[offset], entry.type, 4);
            std::memcpy(&buffer[offset + 4], &entry.offset, 8);
            std::memcpy(&buffer[offset + 12], &entry.size, 8);
        }
        
        return buffer;
    }
    
    /**
     * Deserialize from buffer
     */
    static ChunkDirectory deserialize(const uint8_t* buffer, size_t size) {
        ChunkDirectory dir;
        
        if (size < 4) {
            throw std::runtime_error("ChunkDirectory too small");
        }
        
        uint32_t count;
        std::memcpy(&count, buffer, 4);
        
        if (size < 4 + count * 20) {
            throw std::runtime_error("ChunkDirectory truncated");
        }
        
        for (uint32_t i = 0; i < count; i++) {
            ChunkEntry entry;
            size_t offset = 4 + i * 20;
            std::memcpy(entry.type, &buffer[offset], 4);
            std::memcpy(&entry.offset, &buffer[offset + 4], 8);
            std::memcpy(&entry.size, &buffer[offset + 12], 8);
            dir.entries.push_back(entry);
        }
        
        return dir;
    }
    
    /**
     * Calculate serialized size
     */
    size_t serialized_size() const {
        return 4 + entries.size() * 20;
    }
};

/**
 * QMAT Chunk (Quantization Matrix)
 */
struct QMATChunk {
    uint8_t quality;        // 1..100
    uint8_t num_tables;     // 1 (grayscale) or 3 (YCbCr)
    uint16_t quant_y[64];   // Y/Grayscale table (zigzag order)
    uint16_t quant_cb[64];  // Cb table (optional)
    uint16_t quant_cr[64];  // Cr table (optional)
    
    QMATChunk() : quality(50), num_tables(1) {
        std::memset(quant_y, 0, sizeof(quant_y));
        std::memset(quant_cb, 0, sizeof(quant_cb));
        std::memset(quant_cr, 0, sizeof(quant_cr));
    }
    
    /**
     * Serialize to buffer
     */
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;
        buffer.push_back(quality);
        buffer.push_back(num_tables);
        
        // Y table (always present)
        const uint8_t* y_ptr = reinterpret_cast<const uint8_t*>(quant_y);
        buffer.insert(buffer.end(), y_ptr, y_ptr + 128);
        
        // Cb/Cr tables (if present)
        if (num_tables == 3) {
            const uint8_t* cb_ptr = reinterpret_cast<const uint8_t*>(quant_cb);
            const uint8_t* cr_ptr = reinterpret_cast<const uint8_t*>(quant_cr);
            buffer.insert(buffer.end(), cb_ptr, cb_ptr + 128);
            buffer.insert(buffer.end(), cr_ptr, cr_ptr + 128);
        }
        
        return buffer;
    }
    
    /**
     * Deserialize from buffer
     */
    static QMATChunk deserialize(const uint8_t* buffer, size_t size) {
        QMATChunk qmat;
        
        if (size < 2) {
            throw std::runtime_error("QMAT chunk too small");
        }
        
        qmat.quality = buffer[0];
        qmat.num_tables = buffer[1];
        
        size_t expected_size = 2 + 128 * qmat.num_tables;
        if (size < expected_size) {
            throw std::runtime_error("QMAT chunk truncated");
        }
        
        // Y table
        std::memcpy(qmat.quant_y, &buffer[2], 128);
        
        // Cb/Cr tables
        if (qmat.num_tables == 3) {
            std::memcpy(qmat.quant_cb, &buffer[130], 128);
            std::memcpy(qmat.quant_cr, &buffer[258], 128);
        }
        
        return qmat;
    }
};

} // namespace hakonyans
