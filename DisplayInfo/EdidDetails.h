#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace edid {

using Byte = std::uint8_t;
constexpr std::size_t BlockSize = 128;

inline unsigned Read16(const Byte* bytes)
{
    return bytes[0] | (unsigned(bytes[1]) << 8);
}

inline bool HasValidChecksum(const Byte* bytes, std::size_t size = BlockSize)
{
    return (std::accumulate(bytes, bytes + size, 0u) & 0xff) == 0;
}

struct Axis {
    int active;
    double blank;
    double front;
    double sync;

    double Back() const { return blank - front - sync; }
    double Total() const { return active + blank; }
    bool IsValid() const { return active > 0 && blank > 0 && sync > 0 && Back() >= 0; }
};

enum class Sync { AnalogComposite, BipolarAnalogComposite, DigitalComposite, DigitalSeparate };

struct Timing {
    Axis horizontal;
    Axis vertical;
    double clockHz;
    int widthMm;
    int heightMm;
    Sync sync;
    bool positiveH;
    bool positiveV;
    bool interlaced;
    bool preferred;
    bool native;

    int Width() const { return horizontal.active; }
    int Height() const { return vertical.active * (interlaced ? 2 : 1); }
    double LineRate() const { return clockHz / horizontal.Total(); }
    double Rate() const { return LineRate() / vertical.Total(); }
    bool IsValid() const { return clockHz > 0 && horizontal.IsValid() && vertical.IsValid(); }
};

inline Timing DecodeDtd(const Byte* d)
{
    const bool interlaced = (d[17] & 0x80) != 0;
    return {
        {
            ((d[4] & 0xf0) << 4) | d[2],
            double(((d[4] & 0x0f) << 8) | d[3]),
            double(((d[11] & 0xc0) << 2) | d[8]),
            double(((d[11] & 0x30) << 4) | d[9])
        },
        {
            ((d[7] & 0xf0) << 4) | d[5],
            double(((d[7] & 0x0f) << 8) | d[6]) + (interlaced ? 0.5 : 0.0),
            double(((d[11] & 0x0c) << 2) | (d[10] >> 4)),
            double(((d[11] & 0x03) << 4) | (d[10] & 0x0f))
        },
        Read16(d) * 10000.0,
        ((d[14] & 0xf0) << 4) | d[12],
        ((d[14] & 0x0f) << 8) | d[13],
        static_cast<Sync>((d[17] >> 3) & 3),
        (d[17] & 2) != 0,
        (d[17] & 4) != 0,
        interlaced,
        false,
        false
    };
}

inline Timing DecodeDisplayIdTiming(const Byte* d, bool typeVII, int revision)
{
    const bool interlaced = (d[3] & 0x10) != 0;
    const double fields = interlaced ? 2.0 : 1.0;
    return {
        { int(Read16(d + 4)) + 1, Read16(d + 6) + 1.0,
          (Read16(d + 8) & 0x7fff) + 1.0, Read16(d + 10) + 1.0 },
        { (int(Read16(d + 12)) + 1) / int(fields), (Read16(d + 14) + 1) / fields,
          ((Read16(d + 16) & 0x7fff) + 1) / fields, (Read16(d + 18) + 1) / fields },
        (1 + Read16(d) + (unsigned(d[2]) << 16)) * (typeVII ? 1000.0 : 10000.0),
        0, 0, Sync::DigitalSeparate,
        (d[9] & 0x80) != 0, (d[17] & 0x80) != 0, interlaced,
        revision < 2 && (d[3] & 0x80) != 0,
        false
    };
}

template<typename H, typename V>
void PrintTimingRow(std::ostream& out, const char* label, H horizontal, V vertical)
{
    out << "    " << std::left << std::setw(16) << label << std::right
        << std::setw(17) << horizontal << std::setw(24) << vertical << '\n';
}

inline void PrintTiming(std::ostream& out, const std::string& source, const Timing& timing)
{
    out << "\n  " << source << "  " << timing.Width() << " x " << timing.Height()
        << (timing.interlaced ? 'i' : 'p') << " @ " << std::fixed << std::setprecision(3)
        << timing.Rate() << " Hz" << (timing.interlaced ? " (field rate)" : "")
        << (timing.preferred ? " [preferred]" : "") << (timing.native ? " [native]" : "") << '\n'
        << "    Pixel clock   " << timing.clockHz / 1e6
        << " MHz\n    H scan        " << timing.LineRate() / 1000
        << " kHz\n" << std::defaultfloat << std::setprecision(8);

    const char* syncNames[] = {
        "Analog composite", "Bipolar analog composite", "Digital composite", "Digital separate"
    };
    out << "    Sync          " << syncNames[static_cast<int>(timing.sync)] << "\n\n";
    const auto& h = timing.horizontal;
    const auto& v = timing.vertical;
    PrintTimingRow(out, "", "Horizontal (px)",
        timing.interlaced ? "Vertical (lines/field)" : "Vertical (lines/frame)");
    PrintTimingRow(out, "Active", h.active, v.active);
    PrintTimingRow(out, "Blanking", h.blank, v.blank);
    PrintTimingRow(out, "Total", h.Total(), v.Total());
    PrintTimingRow(out, "Front porch", h.front, v.front);
    PrintTimingRow(out, "Sync width", h.sync, v.sync);
    PrintTimingRow(out, "Back porch", h.Back(), v.Back());
    if (timing.sync == Sync::DigitalComposite || timing.sync == Sync::DigitalSeparate)
        PrintTimingRow(out, "Sync polarity", timing.positiveH ? '+' : '-',
            timing.sync == Sync::DigitalSeparate ? (timing.positiveV ? '+' : '-') : ' ');
    out << std::setprecision(6);
}

struct Mode {
    int width;
    int height;
    int hz;
    bool interlaced;
};

inline void PrintMode(std::ostream& out, const Mode& mode)
{
    out << mode.width << 'x' << mode.height << (mode.interlaced ? 'i' : 'p')
        << " @ " << mode.hz << " Hz nominal";
    if (mode.interlaced)
        out << " (field rate)";
}

inline void PrintStandardTimings(std::ostream& out, const Byte* data, int count, unsigned revision)
{
    constexpr int numerators[] = {10, 3, 4, 9};
    constexpr int denominators[] = {16, 4, 5, 16};
    for (int i = 0; i < count; ++i, data += 2) {
        if (data[0] < 0x20)
            continue;
        const int width = (data[0] + 31) * 8;
        const int aspect = data[1] >> 6;
        const int height = aspect == 0 && revision < 3 ? width :
            (width * numerators[aspect] + denominators[aspect] / 2) / denominators[aspect];
        out << "  Standard timing: " << width << 'x' << height
            << " @ " << (data[1] & 63) + 60 << " Hz nominal\n";
    }
}

inline void PrintEstablishedIII(std::ostream& out, const Byte* bits)
{
    constexpr Mode modes[] = {
        {640,350,85}, {640,400,85}, {720,400,85}, {640,480,85},
        {848,480,60}, {800,600,85}, {1024,768,85}, {1152,864,75},
        {1280,768,60}, {1280,768,60}, {1280,768,75}, {1280,768,85},
        {1280,960,60}, {1280,960,85}, {1280,1024,60}, {1280,1024,85},
        {1360,768,60}, {1440,900,60}, {1440,900,60}, {1440,900,75},
        {1440,900,85}, {1400,1050,60}, {1400,1050,60}, {1400,1050,75},
        {1400,1050,85}, {1680,1050,60}, {1680,1050,60}, {1680,1050,75},
        {1680,1050,85}, {1600,1200,60}, {1600,1200,65}, {1600,1200,70},
        {1600,1200,75}, {1600,1200,85}, {1792,1344,60}, {1792,1344,75},
        {1856,1392,60}, {1856,1392,75}, {1920,1200,60}, {1920,1200,60},
        {1920,1200,75}, {1920,1200,85}, {1920,1440,60}, {1920,1440,75}
    };
    for (std::size_t i = 0; i < std::size(modes); ++i) {
        if (bits[i / 8] & (0x80 >> (i % 8))) {
            out << "  Established timing III: ";
            PrintMode(out, modes[i]);
            if (i == 8 || i == 17 || i == 21 || i == 25 || i == 38)
                out << " (reduced blanking)";
            out << '\n';
        }
    }
}

inline void PrintBaseDetails(std::ostream& out, const Byte* base, const std::vector<Timing>& timings,
    int nativeCount = 0)
{
    constexpr Mode established[] = {
        {720,400,70}, {720,400,88}, {640,480,60}, {640,480,67},
        {640,480,72}, {640,480,75}, {800,600,56}, {800,600,60},
        {800,600,72}, {800,600,75}, {832,624,75}, {1024,768,87,true},
        {1024,768,60}, {1024,768,70}, {1024,768,75}, {1280,1024,75},
        {1152,870,75}
    };
    const unsigned revision = base[19];
    out << "\n[Base EDID]\n";

    for (std::size_t i = 0; i < std::size(established); ++i) {
        if (base[35 + i / 8] & (0x80 >> (i % 8))) {
            out << "  Established timing: ";
            PrintMode(out, established[i]);
            out << '\n';
        }
    }

    PrintStandardTimings(out, base + 38, 8, revision);
    for (int offset = 54; offset <= 108; offset += 18) {
        const Byte* d = base + offset;
        if (Read16(d) || d[2] != 0)
            continue;
        if (d[3] == 0xfa && d[4] == 0)
            PrintStandardTimings(out, d + 5, 6, revision);
        if (d[3] == 0xf7 && d[4] == 0 && d[5] == 0x0a)
            PrintEstablishedIII(out, d + 6);
        if (d[3] != 0xfd)
            continue;

        const int flags = revision >= 4 ? d[4] : 0;
        out << "  Display range limits: "
            << d[5] + ((flags & 3) == 3 ? 255 : 0) << '-'
            << d[6] + (flags & 2 ? 255 : 0) << " Hz vertical, "
            << d[7] + ((flags & 12) == 12 ? 255 : 0) << '-'
            << d[8] + (flags & 8 ? 255 : 0) << " kHz horizontal\n";
        if (d[9]) {
            const double correction = revision >= 4 && d[10] == 4 ? (d[12] >> 2) * 0.25 : 0.0;
            out << "    Maximum pixel clock: " << d[9] * 10 - correction << " MHz\n";
        }
    }
    for (std::size_t i = 0; i < timings.size(); ++i) {
        if (timings[i].IsValid()) {
            Timing timing = timings[i];
            timing.native = int(i) < nativeCount;
            PrintTiming(out, "Base DTD " + std::to_string(i + 1), timing);
        }
    }
}

// CTA VICs are grouped by resolution and scan type. Entries in each group
// give the nominal field/frame rates in VIC order.
struct VicGroup {
    int first;
    int width;
    int height;
    bool interlaced;
    std::initializer_list<int> rates;
};

inline Mode LookupVic(int vic)
{
    static const VicGroup groups[] = {
        {1,640,480,false,{60}}, {2,720,480,false,{60,60}},
        {4,1280,720,false,{60}}, {5,1920,1080,true,{60}},
        {6,1440,480,true,{60,60}}, {8,1440,240,false,{60,60}},
        {10,2880,480,true,{60,60}}, {12,2880,240,false,{60,60}},
        {14,1440,480,false,{60,60}}, {16,1920,1080,false,{60}},
        {17,720,576,false,{50,50}}, {19,1280,720,false,{50}}, {20,1920,1080,true,{50}},
        {21,1440,576,true,{50,50}}, {23,1440,288,false,{50,50}},
        {25,2880,576,true,{50,50}}, {27,2880,288,false,{50,50}},
        {29,1440,576,false,{50,50}}, {31,1920,1080,false,{50,24,25,30}},
        {35,2880,480,false,{60,60}}, {37,2880,576,false,{50,50}},
        {39,1920,1080,true,{50,100}}, {41,1280,720,false,{100}},
        {42,720,576,false,{100,100}}, {44,1440,576,true,{100,100}},
        {46,1920,1080,true,{120}}, {47,1280,720,false,{120}},
        {48,720,480,false,{120,120}}, {50,1440,480,true,{120,120}},
        {52,720,576,false,{200,200}}, {54,1440,576,true,{200,200}},
        {56,720,480,false,{240,240}}, {58,1440,480,true,{240,240}},
        {60,1280,720,false,{24,25,30}}, {63,1920,1080,false,{120,100}},
        {65,1280,720,false,{24,25,30,50,60,100,120}},
        {72,1920,1080,false,{24,25,30,50,60,100,120}},
        {79,1680,720,false,{24,25,30,50,60,100,120}},
        {86,2560,1080,false,{24,25,30,50,60,100,120}},
        {93,3840,2160,false,{24,25,30,50,60}}, {98,4096,2160,false,{24,25,30,50,60}},
        {103,3840,2160,false,{24,25,30,50,60}}, {108,1280,720,false,{48,48}},
        {110,1680,720,false,{48}}, {111,1920,1080,false,{48,48}},
        {113,2560,1080,false,{48}}, {114,3840,2160,false,{48}},
        {115,4096,2160,false,{48}}, {116,3840,2160,false,{48,100,120,100,120}},
        {121,5120,2160,false,{24,25,30,48,50,60,100}}, {193,5120,2160,false,{120}},
        {194,7680,4320,false,{24,25,30,48,50,60,100,120,24,25,30,48,50,60,100,120}},
        {210,10240,4320,false,{24,25,30,48,50,60,100,120}}, {218,4096,2160,false,{100,120}}
    };
    for (const VicGroup& group : groups) {
        const int index = vic - group.first;
        if (index >= 0 && index < int(group.rates.size()))
            return {group.width, group.height, group.rates.begin()[index], group.interlaced};
    }
    return {};
}

inline int SvdVic(Byte svd)
{
    return svd >= 129 && svd <= 192 ? svd & 127 : svd;
}

inline void PrintVic(std::ostream& out, int vic, const std::string& flags)
{
    const Mode mode = LookupVic(vic);
    const std::string resolution = mode.width ?
        std::to_string(mode.width) + " x " + std::to_string(mode.height) + (mode.interlaced ? 'i' : 'p') : "-";
    const std::string rate = mode.width ? std::to_string(mode.hz) + " Hz" : "-";
    out << "  " << std::right << std::setw(4) << vic << "  " << std::left
        << std::setw(16) << resolution << std::setw(14) << rate << flags
        << (mode.interlaced ? (flags.empty() ? "Field rate" : "; field rate") : "")
        << std::right << '\n';
}

template<std::size_t N>
void PrintFlags(std::ostream& out, unsigned bits, const char* const (&names)[N])
{
    for (std::size_t i = 0; i < N; ++i)
        if (bits & (1u << i))
            out << ' ' << names[i];
}

inline void PrintHdr(std::ostream& out, const Byte* data, int length)
{
    const char* eotfs[] = {"SDR gamma", "HDR gamma", "PQ (ST 2084)", "HLG"};
    out << "  HDR static metadata\n    EOTFs:";
    PrintFlags(out, data[0], eotfs);
    out << "\n    Static metadata types:";
    for (int bit = 0; bit < 8; ++bit)
        if (data[1] & (1 << bit))
            out << ' ' << bit + 1;
    out << '\n';

    if (length >= 3 && data[2]) {
        const double max = 50.0 * std::exp2(data[2] / 32.0);
        out << "    Desired max luminance: " << max << " cd/m^2\n";
        if (length >= 5 && data[4])
            out << "    Desired min luminance: " << max * std::pow(data[4] / 255.0, 2) / 100 << " cd/m^2\n";
    }
    if (length >= 4 && data[3])
        out << "    Desired max frame-average luminance: "
            << 50.0 * std::exp2(data[3] / 32.0) << " cd/m^2\n";
}

inline bool PrintCta(std::ostream& out, const Byte* block, int precedingDtds, int nativeCount,
    int& dtdCount)
{
    const int revision = block[1];
    const int dtdStart = block[2];
    if (dtdStart && (dtdStart < 4 || dtdStart > 127))
        return false;
    out << "  Revision: " << revision << '\n';
    if (revision >= 2)
        out << "  Color formats: RGB" << (block[3] & 0x20 ? ", YCbCr 4:4:4" : "")
            << (block[3] & 0x10 ? ", YCbCr 4:2:2" : "") << '\n';

    // A capability map indexes SVDs in wire order, including unknown VICs.
    std::vector<Byte> video;
    std::vector<Byte> only420;
    std::vector<Byte> map;
    bool hasMap = false;
    for (int pos = 4; revision >= 3 && pos < dtdStart;) {
        const Byte header = block[pos++];
        const int length = header & 31;
        if (pos + length > dtdStart)
            return false;
        const Byte* data = block + pos;
        pos += length;
        if (header >> 5 == 2) {
            video.insert(video.end(), data, data + length);
            continue;
        }
        if (header >> 5 != 7)
            continue;
        if (!length)
            return false;

        if (data[0] == 0x05) {
            if (length < 3)
                return false;
            const char* colors[] = {"xvYCC601", "xvYCC709", "sYCC601", "opYCC601",
                "opRGB", "BT.2020 cYCC", "BT.2020 YCC", "BT.2020 RGB"};
            const char* extra[] = {"GBD profile P0", "reserved", "reserved", "reserved",
                "Default", "sRGB", "ICtCp", "ST 2113 RGB"};
            out << "  Colorimetry:";
            PrintFlags(out, data[1], colors);
            PrintFlags(out, data[2] & 0xf1, extra);
            out << '\n';
        } else if (data[0] == 0x06) {
            if (length < 3)
                return false;
            PrintHdr(out, data + 1, length - 1);
        } else if (data[0] == 0x0e) {
            only420.insert(only420.end(), data + 1, data + length);
        } else if (data[0] == 0x0f) {
            if (hasMap)
                return false;
            hasMap = true;
            map.assign(data + 1, data + length);
        }
    }
    if (!video.empty() || !only420.empty())
        out << "\n   VIC  Mode            Nominal rate  Flags\n";
    for (std::size_t i = 0; i < video.size(); ++i) {
        const int vic = SvdVic(video[i]);
        const bool supports420 = hasMap && (map.empty() ||
            (i / 8 < map.size() && (map[i / 8] & (1u << (i % 8)))));
        std::string flags = vic != video[i] ? "Native" : "";
        if (supports420)
            flags += flags.empty() ? "YCbCr 4:2:0 supported" : "; YCbCr 4:2:0 supported";
        PrintVic(out, vic, flags);
    }

    for (Byte svd : only420)
        PrintVic(out, SvdVic(svd), "YCbCr 4:2:0 only");

    int pos = dtdStart ? dtdStart : 127;
    for (; pos + 18 <= 127; pos += 18) {
        if (std::all_of(block + pos, block + pos + 18, [](Byte value) { return value == 0; }))
            break;
        if (!Read16(block + pos))
            continue;
        Timing timing = DecodeDtd(block + pos);
        timing.native = precedingDtds + dtdCount < nativeCount;
        if (!timing.IsValid())
            return false;
        PrintTiming(out, "CTA DTD " + std::to_string(++dtdCount), timing);
    }
    return std::all_of(block + (dtdStart ? pos : 4), block + 127, [](Byte value) { return value == 0; });
}

inline const char* DisplayIdBlockName(int tag)
{
    switch (tag) {
    case 0x00: case 0x20: return "Product identification";
    case 0x01: case 0x21: return "Display parameters";
    case 0x02: return "Color characteristics";
    case 0x03: return "Type I detailed timings";
    case 0x04: return "Type II detailed timings";
    case 0x05: return "Type III short timings";
    case 0x06: return "Type IV enumerated timings";
    case 0x09: case 0x25: return "Timing range limits";
    case 0x12: case 0x28: return "Tiled display topology";
    case 0x22: return "Type VII detailed timings";
    case 0x23: return "Type VIII enumerated timings";
    case 0x24: return "Type IX formula timings";
    case 0x26: return "Display interface features";
    case 0x2a: return "Type X formula timings";
    case 0x81: return "CTA data";
    default: return "Data block";
    }
}

inline bool PrintDisplayId(std::ostream& out, const Byte* block)
{
    const int major = block[1] >> 4;
    const int size = block[2];
    if (size > 121 || !HasValidChecksum(block + 1, size + 5))
        return false;
    const int end = 5 + size;
    out << "  Version: " << major << '.' << (block[1] & 15) << '\n';

    for (int pos = 5; pos < end;) {
        if (end - pos < 3)
            return false;
        const int tag = block[pos];
        const int flags = block[pos + 1];
        const int length = block[pos + 2];
        if (!tag && !flags && !length && std::all_of(block + pos, block + end,
                [](Byte value) { return value == 0; }))
            break;
        pos += 3;
        if (length > end - pos)
            return false;
        out << "  " << DisplayIdBlockName(tag) << " (tag 0x"
            << std::hex << tag << std::dec << ", " << length << " bytes)\n";
        const bool typeVII = major == 2 && tag == 0x22;
        if ((major == 1 && tag == 0x03) || typeVII) {
            const int stride = 20 + (typeVII ? (flags >> 4) & 7 : 0);
            if (!length || length % stride)
                return false;
            for (int offset = 0; offset < length; offset += stride) {
                const Timing timing = DecodeDisplayIdTiming(block + pos + offset, typeVII, flags & 7);
                if (!timing.IsValid())
                    return false;
                PrintTiming(out, std::string(typeVII ? "Type VII DTD " : "Type I DTD ") +
                    std::to_string(offset / stride + 1), timing);
            }
        }
        pos += length;
    }
    return true;
}

inline int PrintExtensions(std::ostream& out, std::ostream& errors,
    const std::string& monitor, const std::vector<Byte>& data, int baseDtds)
{
    int nativeCount = -1;
    int precedingDtds = baseDtds;
    for (std::size_t index = 1; index <= data[126]; ++index) {
        const auto offset = index * BlockSize;
        const auto warning = monitor + ": EDID extension block " + std::to_string(index) + ": ";
        if (data.size() < offset + BlockSize) {
            errors << warning << "missing or truncated\n";
            continue;
        }
        const Byte* block = data.data() + offset;
        if (!block[0] || !HasValidChecksum(block)) {
            errors << warning << "invalid tag or checksum\n";
            continue;
        }
        std::ostringstream content;
        bool valid = true;
        switch (block[0]) {
        case 0x02: {
            // The count covers DTDs in wire order, starting in the base EDID.
            const int count = nativeCount >= 0 ? nativeCount : (block[1] >= 2 ? block[3] & 15 : 0);
            int dtdCount = 0;
            valid = PrintCta(content, block, precedingDtds, count, dtdCount);
            if (valid) {
                nativeCount = count;
                precedingDtds += dtdCount;
            }
            break;
        }
        case 0x70: valid = PrintDisplayId(content, block); break;
        default:
            content << "  Tag: 0x" << std::hex << unsigned(block[0]) << '\n';
            break;
        }
        if (valid)
            out << "\n[" << (block[0] == 0x02 ? "CTA-861" : block[0] == 0x70 ? "DisplayID" : "EDID")
                << " / Extension " << index << "]\n" << content.str();
        else
            errors << warning << "malformed payload\n";
    }
    if (data.size() > (std::size_t(data[126]) + 1) * BlockSize)
        errors << monitor << ": EDID contains data beyond its declared extension count\n";
    return nativeCount;
}

} // namespace edid
