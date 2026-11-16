#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstddef>
#include <format>
#include <string>
#include <zlib.h>
#include <cmath>

#include "raylib.h"

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
    std::vector<uint8_t> image_data;
    uint32_t crc;
};

struct ImagePNG {
    // 0 is an invalid value for w and h
    int32_t width = 0;
    int32_t height = 0;
    std::byte bit_depth{0x0}; // 1, 2, 4, 8, 16 - not all are allowed for any color type
    std::byte color_type{0x0}; // 0, 2, 3, 4, 6
    std::byte compression_method{0x0}; // only compression method 0 is defined
    std::byte filter_method{0x0}; // only filter method 0 is defined
    std::byte interlace_method{0x0}; // only interlace method 0 and 1 are defined
    std::vector<Chunk> chunks;
    bool has_plte = false;
};

/* color type || allowed bit depths
        0     |    1, 2, 4, 8, 16  - each pixel is a grayscale sample
        2     |    8, 16           - each pixel is an RGB triple
        3     |    1, 2, 4, 8      - each pixel is a pallete index, PLTE chunk must appear
        4     |    8, 16           - each pixel is a grayscale sample, followed by an alpha sample
        6     |    8, 16           - each pixel is an RGB triple, followed by an alpha sample
*/
// Sample depth is always the same as bit depth, instead of case 3 where it is 8 bits

// png file structure
// length | chunk type | chunk data | CRC
// 4B uint| 4B         | length in B| 4B

// constants
constexpr uint32_t ihdr_length = 13u;
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

constexpr uint32_t d(const int val) { return (1u << val); }
constexpr uint32_t color_type_infos[7] = {
    { d(1)|d(2)|d(4)|d(8)|d(16) }, // 0
    { 0 }, // 1 not possible
    { d(8)|d(16) }, // 2
    { d(1)|d(2)|d(4)|d(8) }, // 3
    { d(8)|d(16) }, // 4
    { 0 }, // 5 not possible
    { d(8)|d(16) } // 6
};

void verify_ihdr(const ImagePNG& image) {
    auto& first_chunk = image.chunks.front();
    if (first_chunk.chunk_type != IHDR) throw std::runtime_error("Corrupted file. IHDR chunk expected to be first.\n");
    if (first_chunk.length != 13) throw std::runtime_error("Corrupted file. IHDR chunk expected to have a length of 13.\n");
    if (image.width <= 0 || image.height <= 0) throw std::runtime_error("Corrupted file. Width and height should not be 0 or negative.\n");
    // check color types
    if (std::to_integer<uint8_t>(image.color_type) > 6) throw std::runtime_error("Corrupted file. Color type must be 6 or below");
    for (uint32_t i = 0; i <= 6; i++) {
        if (std::to_integer<uint32_t>(image.color_type) != i) continue;
        if (std::to_integer<uint32_t>(image.bit_depth) & color_type_infos[i])
            throw std::runtime_error(std::format("Corrupted file. Color type and bit depth do not match for color type: {}\n",
                std::to_integer<uint32_t>(image.color_type)));
        break;
    }
    if (std::to_integer<uint8_t>(image.compression_method) != 0u) throw std::runtime_error("Corrupted file. Compression method is expected to be 0.");
    if (std::to_integer<uint8_t>(image.filter_method) != 0u) throw std::runtime_error("Corrupted file. Filter method is expected to be 0.");
    if (!(std::to_integer<uint8_t>(image.filter_method) == 0u || std::to_integer<uint8_t>(image.filter_method) == 1u))
        throw std::runtime_error("Corrupted file. Filter method is expected to be 0.");
}

void verify_iend(const ImagePNG& image) {
    auto& last_chunk = image.chunks.back();
    if (last_chunk.chunk_type != IEND) throw std::runtime_error("Corrupted file. IEND chunk expected to be last.\n");
    if (last_chunk.length != 0) throw std::runtime_error("Corrupted file. IEND length should be 0.\n");
    if (!last_chunk.image_data.empty()) throw std::runtime_error("Corrupted file. IEND image data should be empty.\n");
}

const Chunk& find_plte(const ImagePNG& image) {
    for (auto& chunk : image.chunks) if (chunk.chunk_type == PLTE) return chunk;
}

bool has_multiple_plte(const ImagePNG& image) {
    int count = 0;
    for (auto& chunk : image.chunks) if (chunk.chunk_type == PLTE) count ++;
    return count > 1;
}
void verify_plte(const ImagePNG& image) {
    // there must be only 1 plte chunk
    if (has_multiple_plte(image)) throw std::runtime_error("Corrupted file. PLTE chunk can be present only once.\n");
    // has to be with type 3, cannot be with type 0 and 4
    const int c_type = std::to_integer<int>(image.color_type);
    if (!image.has_plte && c_type == 3) throw std::runtime_error("Corrupted file. PLTE chunk has to be present with color type 3!\n");
    if (image.has_plte && (c_type == 0 || c_type == 4)) throw std::runtime_error(std::format("Corrupted file. PLTE chunk cannot be present with color type: {}!\n", c_type));
    // if there is no plte, we can return
    if (!image.has_plte) return;
    auto& plte_chunk = find_plte(image);
    if (plte_chunk.length % 3 != 0) throw std::runtime_error("Corrupted file. PLTE chunk length is expected to be divisible by 3");
}

bool decompress_dynamic(const std::vector<uint8_t>& compressed_data, std::vector<uint8_t>& out_decompressed_data) {
    z_stream stream;

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;

    int err = inflateInit2(&stream, MAX_WBITS);
    if (err != Z_OK) {
        std::printf("Error during decompression: %d", err);
        return false;
    }
    std::printf("Initialized inflate stream\n");

    stream.avail_in = compressed_data.size();
    stream.next_in = const_cast<Bytef*>(compressed_data.data());

    std::vector<uint8_t> out_buf;
    const size_t chunk_size = 32768; // 32KB
    std::vector<uint8_t> temp_buf(chunk_size);

    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = temp_buf.data();
        stream.avail_out = chunk_size;

        status = inflate(&stream, Z_NO_FLUSH);

        if (status == Z_OK || status == Z_STREAM_END) {
            size_t b_decompressed =  temp_buf.size() - stream.avail_out;
            out_buf.insert(out_buf.end(), temp_buf.begin(), temp_buf.begin() + b_decompressed);
        }

        if (status == Z_STREAM_END) break;
        if (status != Z_OK) {
            inflateEnd(&stream);
            std::printf("Decompression failed with error: %s", std::to_string(status).c_str());
            return false;
        }
    }
    std::printf("Inflate written\n");

    inflateEnd(&stream);
    std::printf("Inflate ended\n");
    out_decompressed_data = std::move(out_buf);
    std::printf("Decompressed data written\n");

    return true;
}

static int format_for(int color_type) {
    switch (color_type) {
        case 0: return PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
        case 2: return PIXELFORMAT_UNCOMPRESSED_R8G8B8;
        case 4: return PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA;
        case 6: return PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        default: throw std::runtime_error(std::format("No format for color type of: {}", color_type));
    }
}

static int channels_for(int color_type) {
    switch (color_type) {
        case 0: return 1; // grayscale
        case 2: return 3; // RGB
        case 3: return 1; // palette index
        case 4: return 2; // grayscale + alpha
        case 6: return 4; // RGBA
        default: throw std::runtime_error("Invalid color type.\n");
    }
}

static uint8_t paeth(int a, int b, int c) {
    const int p  = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

void unfilter(const std::vector<uint8_t>& uncompressed_data, const ImagePNG& image_info, std::vector<uint8_t>& out_unfiltered_data) {
    if (std::to_integer<uint8_t>(image_info.interlace_method) != 0) throw std::runtime_error("Interlace methods other than 0 are not supported.\n");

    const size_t width = static_cast<size_t>(image_info.width);
    const size_t height = static_cast<size_t>(image_info.height);
    const int bit_depth = std::to_integer<uint8_t>(image_info.bit_depth);
    const int channels = channels_for(std::to_integer<uint8_t>(image_info.color_type));

    const size_t bits_per_pixel = static_cast<size_t>(channels) * bit_depth;
    const size_t bpp = std::max<size_t>(1, bits_per_pixel / 8);
    const size_t row_bytes = (width * bits_per_pixel + 7) / 8;
    const size_t stride = row_bytes + 1;

    if (uncompressed_data.size() != height * stride) throw std::runtime_error("Decompressed size does not match image dimensions.\n");

    out_unfiltered_data.assign(height * row_bytes, 0);

    for (size_t y = 0; y < height; y++) {
        const uint8_t filter = uncompressed_data[y * stride];
        const uint8_t* src = &uncompressed_data[y * stride + 1];
        uint8_t* cur = &out_unfiltered_data[y * row_bytes];
        const uint8_t* prev = (y > 0) ? &out_unfiltered_data[(y - 1) * row_bytes] : nullptr;

        for (size_t x = 0; x < row_bytes; x++) {
            const int a = (x >= bpp) ? cur[x - bpp] : 0;
            const int b = prev ? prev[x] : 0;
            const int c = (prev && x >= bpp) ? prev[x - bpp] : 0;

            switch (filter) {
                case 0: cur[x] = src[x]; break;
                case 1: cur[x] = static_cast<uint8_t>(src[x] + a); break;
                case 2: cur[x] = static_cast<uint8_t>(src[x] + b); break;
                case 3: cur[x] = static_cast<uint8_t>(src[x] + ((a + b) / 2)); break;
                case 4: cur[x] = static_cast<uint8_t>(src[x] + paeth(a, b, c)); break;
                default: throw std::runtime_error("Invalid filter type in scanline.\n");
            }
        }
    }
}

void draw_png(const std::vector<uint8_t>& unfiltered_data, const ImagePNG& image_info) {

}

// ReSharper disable once CppDFAConstantConditions
int main() {
    ImagePNG image;
    std::string sample_loc = resourcePath("sample3.png");
    std::ifstream file(sample_loc, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("File could not be loaded!\n");

    // filesize
    std::streamsize file_size = file.tellg();
    if (file_size <= 0) throw std::runtime_error("File size is 0 or less!\n");

    // verifying file signature
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(png_file_sign.size());
    if (!file.read(reinterpret_cast<char*>(buffer.data()), png_file_sign.size())) throw std::runtime_error("Couldn't read file!\n");
    if (buffer != png_file_sign) throw std::runtime_error("File is not a valid png!\n");

    // image chunks
    std::vector<Chunk> image_chunks;
    bool shouldExit = false;
    int chunk_count = 0;
    do {
        // get length of chunk
        uint32_t length{0u};
        if (!file.read(reinterpret_cast<char*>(&length), 4)) throw std::runtime_error("Error reading file length,\n");
        length = swap32(length);
        if (length > 0x7FFFFFFF) throw std::runtime_error("File is corrupted. Unrealistic or negative chunk length.\n");
        // get chunk type
        uint32_t chunk_type{0u};
        if (!file.read(reinterpret_cast<char*>(&chunk_type), 4)) throw std::runtime_error("Error reading chunk type.\n");
        chunk_type = swap32(chunk_type);
        if (chunk_type == IEND) shouldExit = true;
        if (chunk_type == IHDR) {
            if (!file.read(reinterpret_cast<char*>(&image.width), 4)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            if (!file.read(reinterpret_cast<char*>(&image.height), 4)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            if (!file.read(reinterpret_cast<char*>(&image.bit_depth), 1)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            if (!file.read(reinterpret_cast<char*>(&image.color_type), 1)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            if (!file.read(reinterpret_cast<char*>(&image.compression_method), 1)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            if (!file.read(reinterpret_cast<char*>(&image.filter_method), 1)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            if (!file.read(reinterpret_cast<char*>(&image.interlace_method), 1)) throw std::runtime_error("Error parsing IHDR chunk.\n");
            file.seekg(-13, std::ios::cur);
        }
        if (chunk_type == PLTE) image.has_plte = true;
        // read chunk data using length
        std::vector<uint8_t> image_data(length);
        if (length > 0u) if (!file.read(reinterpret_cast<char*>(image_data.data()), length)) throw std::runtime_error("Error reading image data.\n");
        // read crc
        uint32_t crc{0u};
        if (!file.read(reinterpret_cast<char*>(&crc), 4)) throw std::runtime_error("Error reading chunk CRC.\n");
        crc = swap32(crc);

        image_chunks.emplace_back(Chunk{length, chunk_type, std::move(image_data), crc});
        chunk_count++;
    } while (!shouldExit);

    image.width = swap32(image.width);
    image.height = swap32(image.height);
    image.chunks = std::move(image_chunks);

    // verify
    verify_ihdr(image);
    verify_iend(image);
    verify_plte(image);

    // decompression
    std::vector<uint8_t> compressed_image_data;
    std::vector<uint8_t> uncompressed_image_data;
    std::vector<uint8_t> unfiltered_image_data;
    for (int i = 0; i < image.chunks.size(); i++) {
        if (image.chunks[i].chunk_type != IDAT) continue;
        compressed_image_data.insert(compressed_image_data.end(), image.chunks[i].image_data.begin(), image.chunks[i].image_data.end());
    }
    if (!decompress_dynamic(compressed_image_data, uncompressed_image_data)) throw std::runtime_error("Error during decompression!");
    compressed_image_data.clear();
    unfilter(uncompressed_image_data, image, unfiltered_image_data);
    uncompressed_image_data.clear();

    std::printf("Width: %d, Height: %d\n", image.width, image.height);
    std::printf("Bit depth: %d\n", image.bit_depth);
    std::printf("Color Type: %d\n", image.color_type);
    std::printf("Compression Method: %d\n", image.compression_method);
    std::printf("Filter Method: %d\n", image.filter_method);
    std::printf("Interlace Method: %d\n", image.interlace_method);
    for (auto&[length, chunk_type, image_data, crc] : image.chunks) {
        std::printf("[");
        std::printf("%u, ", length);
        std::printf("%.4s, ", reinterpret_cast<char*>(&chunk_type));
        std::printf("%llu, ", image_data.size());
        std::printf("%u", crc);
        std::printf("]\n");
    }

    // rendering
    InitWindow(1280, 720, "PNG Parser");

    auto texture_image = Image(unfiltered_image_data.data(), image.width, image.height, 1, format_for(std::to_integer<int>(image.color_type)));
    Texture2D texture = LoadTextureFromImage(texture_image);

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(WHITE);
            DrawTextureEx(texture, {2, 2}, 0.0f, 1.0f, {255, 255, 255, 255});
        EndDrawing();
    }

    UnloadTexture(texture);
    CloseWindow();

    return 0;
}
