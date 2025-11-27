// SDMetadataExtractor - Extracts Stable Diffusion parameters from PNG tEXt/zTXt chunks
// Modern C++20

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <cstdlib>      // for _byteswap_ulong on MSVC
#include <zlib.h>

namespace fs = std::filesystem;

// PNG signature
constexpr uint8_t PNG_SIGNATURE[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

// Portable byte order conversion (no Winsock dependency)
inline uint32_t be32toh(uint32_t big_endian_32bits) {
#ifdef _MSC_VER
    return _byteswap_ulong(big_endian_32bits);
#else
    return ntohl(big_endian_32bits);
#endif
}

static bool is_png(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    uint8_t sig[8]{};
    file.read(reinterpret_cast<char*>(sig), 8);
    return file && std::memcmp(sig, PNG_SIGNATURE, 8) == 0;
}

static std::string decompress_zTXt(const std::vector<uint8_t>& compressed_data) {
    if (compressed_data.size() < 10) return {};

    const uint8_t* ptr = compressed_data.data();
    size_t offset = 0;

    // Skip keyword until null terminator
    while (offset < compressed_data.size() && ptr[offset] != 0) ++offset;
    if (offset >= compressed_data.size() - 1) return {};
    ++offset; // skip null

    if (ptr[offset++] != 0) return {}; // compression method must be 0 (deflate)

    std::vector<uint8_t> compressed(ptr + offset, ptr + compressed_data.size());

    z_stream zs{};
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = static_cast<uInt>(compressed.size());
    zs.next_in = compressed.data();

    if (inflateInit(&zs) != Z_OK) return {};

    std::string result;
    result.reserve(64 * 1024); // typical A1111 parameters size

    std::vector<uint8_t> out_buffer(128 * 1024);
    int ret;

    do {
        zs.avail_out = static_cast<uInt>(out_buffer.size());
        zs.next_out = out_buffer.data();

        ret = inflate(&zs, Z_NO_FLUSH);

        if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return {};
        }

        result.append(reinterpret_cast<char*>(out_buffer.data()), zs.total_out - result.size());
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return (ret == Z_STREAM_END) ? result : std::string{};
}

static bool extract_text_chunks(const fs::path& png_path, std::string& out_metadata) {
    std::ifstream file(png_path, std::ios::binary);
    if (!file) return false;

    uint8_t sig[8]{};
    file.read(reinterpret_cast<char*>(sig), 8);
    if (!file || memcmp(sig, PNG_SIGNATURE, 8) != 0) return false;

    std::vector<uint8_t> buffer;
    out_metadata.clear();

    while (true) {
        uint32_t length_be = 0;
        if (!file.read(reinterpret_cast<char*>(&length_be), 4)) break;
        uint32_t length = be32toh(length_be);

        char chunk_type[5] = { 0 };
        if (!file.read(chunk_type, 4)) break;

        std::string type = chunk_type;

        buffer.resize(length);
        if (length > 0) {
            file.read(reinterpret_cast<char*>(buffer.data()), length);
            if (static_cast<uint32_t>(file.gcount()) != length) break;
        }

        // Skip CRC
        uint32_t crc = 0;
        if (!file.read(reinterpret_cast<char*>(&crc), 4)) break;

        if (type == "tEXt") {
            auto null_pos = std::find(buffer.begin(), buffer.end(), 0);
            if (null_pos != buffer.end()) {
                std::string keyword(buffer.begin(), null_pos);
                std::string text(null_pos + 1, buffer.end());
                if (!out_metadata.empty()) out_metadata += "\n\n";
                out_metadata += keyword + ": " + text;
            }
        }
        else if (type == "zTXt") {
            auto null_pos = std::find(buffer.begin(), buffer.end(), 0);
            if (null_pos != buffer.end()) {
                std::string keyword(buffer.begin(), null_pos);
                std::string decompressed = decompress_zTXt(buffer);
                if (!decompressed.empty()) {
                    if (!out_metadata.empty()) out_metadata += "\n\n";
                    out_metadata += keyword + ": " + decompressed;
                }
            }
        }
        else if (type == "IEND") {
            break;
        }
    }

    return !out_metadata.empty();
}

static void process_folder(const fs::path& folder) {
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "Error: Invalid or inaccessible folder path.\n";
        return;
    }

    int processed = 0, extracted = 0;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        if (path.extension() != ".png" && path.extension() != ".PNG") continue;
        if (!is_png(path)) continue;

        std::string metadata;
        if (extract_text_chunks(path, metadata)) {
            fs::path txt_path = path;
            txt_path.replace_extension(".txt");

            std::ofstream txt_file(txt_path, std::ios::binary);
            if (txt_file) {
                txt_file << metadata;
                ++extracted;
            }
        }

        ++processed;
        std::cout << "\rProcessed: " << processed << " | Metadata found: " << extracted << std::flush;
    }

    std::cout << "\n\nFinished! Scanned " << processed << " PNG files, extracted metadata from " << extracted << ".\n";
}

int main() {
    std::cout << "Stable Diffusion PNG Metadata Extractor (tEXt + zTXt)\n";
    std::cout << "====================================================\n\n";

    std::string input;
    std::cout << "Paste or type the full path to your PNG folder:\n> ";
    std::getline(std::cin, input);

    if (input.empty()) {
        std::cerr << "No path provided.\n";
        return 1;
    }

    // Remove surrounding quotes if copied from Explorer
    if (input.front() == '"' && input.back() == '"') {
        input = input.substr(1, input.size() - 2);
    }

    fs::path folder = fs::path(input).lexically_normal();
    process_folder(folder);

    std::cout << "\nPress Enter to exit...";
    std::cin.get();
    return 0;
}