#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace topopt {

// Minimal, dependency-free 8-bit grayscale PNG writer. Uses stored (uncompressed)
// DEFLATE blocks so no zlib linkage is needed -- fine for small cross-section
// images. Rows are top-to-bottom; pixels[y*width + x] in [0,255].
class PngWriter {
public:
    static bool writeGray(const std::string& path, int width, int height,
                          const std::vector<unsigned char>& pixels) {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        f.write(reinterpret_cast<const char*>(sig), 8);

        std::vector<unsigned char> ihdr;
        putU32(ihdr, static_cast<uint32_t>(width));
        putU32(ihdr, static_cast<uint32_t>(height));
        ihdr.push_back(8);   // bit depth
        ihdr.push_back(0);   // colour type: grayscale
        ihdr.push_back(0);   // compression
        ihdr.push_back(0);   // filter
        ihdr.push_back(0);   // interlace
        writeChunk(f, "IHDR", ihdr);

        // Raw (filtered) scanlines: each row prefixed with filter byte 0.
        std::vector<unsigned char> raw;
        raw.reserve(static_cast<size_t>(height) * (width + 1));
        for (int y = 0; y < height; ++y) {
            raw.push_back(0);
            for (int x = 0; x < width; ++x)
                raw.push_back(pixels[static_cast<size_t>(y) * width + x]);
        }
        writeChunk(f, "IDAT", zlibStored(raw));
        writeChunk(f, "IEND", {});
        return static_cast<bool>(f);
    }

private:
    static void putU32(std::vector<unsigned char>& v, uint32_t x) {
        v.push_back(static_cast<unsigned char>((x >> 24) & 0xff));
        v.push_back(static_cast<unsigned char>((x >> 16) & 0xff));
        v.push_back(static_cast<unsigned char>((x >> 8) & 0xff));
        v.push_back(static_cast<unsigned char>(x & 0xff));
    }

    static uint32_t crc32(const unsigned char* p, size_t n) {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            init = true;
        }
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i)
            c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    static uint32_t adler32(const std::vector<unsigned char>& d) {
        uint32_t a = 1, b = 0;
        for (unsigned char x : d) {
            a = (a + x) % 65521;
            b = (b + a) % 65521;
        }
        return (b << 16) | a;
    }

    // zlib stream wrapping DEFLATE stored blocks (no compression).
    static std::vector<unsigned char> zlibStored(
        const std::vector<unsigned char>& data) {
        std::vector<unsigned char> out;
        out.push_back(0x78);  // CMF
        out.push_back(0x01);  // FLG (no dict, fastest)
        size_t off = 0, n = data.size();
        while (off < n || n == 0) {
            const size_t len = (n - off > 65535) ? 65535 : (n - off);
            const bool final = (off + len >= n);
            out.push_back(final ? 1 : 0);
            out.push_back(static_cast<unsigned char>(len & 0xff));
            out.push_back(static_cast<unsigned char>((len >> 8) & 0xff));
            const uint16_t nlen = static_cast<uint16_t>(~len);
            out.push_back(static_cast<unsigned char>(nlen & 0xff));
            out.push_back(static_cast<unsigned char>((nlen >> 8) & 0xff));
            for (size_t i = 0; i < len; ++i) out.push_back(data[off + i]);
            off += len;
            if (n == 0) break;
        }
        const uint32_t ad = adler32(data);
        putU32(out, ad);
        return out;
    }

    static void writeChunk(std::ofstream& f, const char type[4],
                           const std::vector<unsigned char>& data) {
        unsigned char len[4] = {
            static_cast<unsigned char>((data.size() >> 24) & 0xff),
            static_cast<unsigned char>((data.size() >> 16) & 0xff),
            static_cast<unsigned char>((data.size() >> 8) & 0xff),
            static_cast<unsigned char>(data.size() & 0xff)};
        f.write(reinterpret_cast<const char*>(len), 4);
        f.write(type, 4);
        if (!data.empty())
            f.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
        std::vector<unsigned char> crcbuf(4 + data.size());
        crcbuf[0] = static_cast<unsigned char>(type[0]);
        crcbuf[1] = static_cast<unsigned char>(type[1]);
        crcbuf[2] = static_cast<unsigned char>(type[2]);
        crcbuf[3] = static_cast<unsigned char>(type[3]);
        for (size_t i = 0; i < data.size(); ++i) crcbuf[4 + i] = data[i];
        const uint32_t c = crc32(crcbuf.data(), crcbuf.size());
        unsigned char cb[4] = {static_cast<unsigned char>((c >> 24) & 0xff),
                               static_cast<unsigned char>((c >> 16) & 0xff),
                               static_cast<unsigned char>((c >> 8) & 0xff),
                               static_cast<unsigned char>(c & 0xff)};
        f.write(reinterpret_cast<const char*>(cb), 4);
    }
};

} // namespace topopt
