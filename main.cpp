#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstddef>
#include <string>

uint32_t swap32(uint32_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#elif defined(_MSC_VER)
    return _byteswap_ulong(val);
#endif
}

#ifndef RESOURCE_DIR
#define RESOURCE_DIR "res"
#endif
inline std::string resourcePath(const std::string& relative) {
    return std::string(RESOURCE_DIR) + "/" + relative;
}

struct Chunk {
    uint32_t length;
    uint32_t chunk_type;
    std::vector<std::byte> image_data;
    uint32_t crc;
};

struct Image {
    int32_t width = 0, height = 0;
    std::byte bit_depth{0x0}, color_type{0x0}, compression_method{0x0}, filter_method{0x0}, interlace_method{0x0};
    std::vector<Chunk> chunks;
};

// png file structure
// length | chunk type | chunk data | CRC
// 4B uint| 4B         | length in B| 4B

// constants
constexpr uint32_t IEND = 0x49454E44;
constexpr uint32_t IHDR = 0x49484452;
constexpr uint32_t IDAT = 0x49444154;
constexpr uint32_t PLTE = 0x504C5445;
const std::vector<uint8_t> png_file_sign{ 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};

/* Table of CRCs of all 8-bit messages. */
unsigned long crc_table[256];
int crc_table_computed = 0;
/* Make the table for a fast CRC. */
void make_crc_table() {
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long) n;
        for (k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_computed = 1;
}
/* Update a running CRC with the bytes buf[0..len-1]--the CRC
   should be initialized to all 1's, and the transmitted value
   is the 1's complement of the final running CRC (see the
   crc() routine below)). */
unsigned long update_crc(unsigned long crc, unsigned char *buf, int len) {
    unsigned long c = crc;
    int n;
    if (!crc_table_computed)
        make_crc_table();
    for (n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}
/* Return the CRC of the bytes buf[0..len-1]. */
unsigned long crc(unsigned char *buf, int len) {
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

// ReSharper disable once CppDFAConstantConditions
int main() {
    Image image;
    const char* sample_loc = resourcePath("sample.png").c_str();
    std::ifstream file(sample_loc, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Error opening file for reading.\n";
        return 1;
    }

    // filesize
    std::streamsize file_size = file.tellg();
    if (file_size <= 0) {
        std::cerr << "File is empty.\n";
        return 1;
    }

    // verifying file signature
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(png_file_sign.size());
    if (!file.read(reinterpret_cast<char*>(buffer.data()), png_file_sign.size())) {
        std::cerr << "Cannot read file!\n";
        return 1;
    }
    if (buffer != png_file_sign) {
        std::cerr << "File is not a valid .png!\n";
        return 1;
    }

    // image chunks
    std::vector<Chunk> image_chunks;
    bool shouldExit = false;
    do {
        // get length of chunk
        uint32_t length{0u};
        if (!file.read(reinterpret_cast<char*>(&length), 4)) {
            std::cerr << "Error reading chunk length.\n";
            return 1;
        }
        length = swap32(length);
        if (length > 0x7FFFFFFF) {
            std::cerr << "File is corrupted! Unrealistic or negative chunk length.\n";
            return 1;
        }
        // get chunk type
        uint32_t chunk_type{0u};
        if (!file.read(reinterpret_cast<char*>(&chunk_type), 4)) {
            std::cerr << "Error reading chunk type.\n";
            return 1;
        }
        chunk_type = swap32(chunk_type);
        if (chunk_type == IEND) shouldExit = true;
        if (chunk_type == IHDR) {
            if (!file.read(reinterpret_cast<char*>(&image.width), 4)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            if (!file.read(reinterpret_cast<char*>(&image.height), 4)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            if (!file.read(reinterpret_cast<char*>(&image.bit_depth), 1)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            if (!file.read(reinterpret_cast<char*>(&image.color_type), 1)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            if (!file.read(reinterpret_cast<char*>(&image.compression_method), 1)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            if (!file.read(reinterpret_cast<char*>(&image.filter_method), 1)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            if (!file.read(reinterpret_cast<char*>(&image.interlace_method), 1)) {
                std::cerr << "Error parsing IHDR chunk";
                return 1;
            }
            file.seekg(-13, std::ios::cur);
        }
        // read chunk data using length
        std::vector<std::byte> image_data(length);
        if (length > 0u) {
            if (!file.read(reinterpret_cast<char*>(image_data.data()), length)) {
                std::cerr << "Error reading chunk image data.\n";
                return 1;
            }
        }
        // read crc
        uint32_t crc{0u};
        if (!file.read(reinterpret_cast<char*>(&crc), 4)) {
            std::cerr << "Error reading CRC.\n";
            return 1;
        }
        crc = swap32(crc);

        image_chunks.emplace_back(Chunk{length, chunk_type, std::move(image_data), crc});
    } while (!shouldExit);

    image.width = swap32(image.width);
    image.height = swap32(image.height);
    image.chunks = std::move(image_chunks);

    std::printf("Width: %d, Height: %d\n", image.width, image.height);
    for (auto&[length, chunk_type, image_data, crc] : image.chunks) {
        std::printf("[");
        std::printf("%u, ", length);
        std::printf("%.4s, ", reinterpret_cast<char*>(&chunk_type));
        std::printf("%llu, ", image_data.size());
        std::printf("%u", crc);
        std::printf("]\n");
    }

    return 0;
}
