#include "../DisplayInfo/EdidDetails.h"

#include <array>
#include <cassert>
#include <iostream>
#include <regex>

using namespace edid;

using Block = std::array<Byte, BlockSize>;

constexpr Byte Progressive[] = {
    0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40,
    88, 44, 0x45, 0, 0xfd, 0x28, 0x11, 0, 0, 0x1e
};

void Seal(Byte* data, std::size_t size)
{
    data[size - 1] = 0;
    data[size - 1] = Byte(0u - std::accumulate(data, data + size, 0u));
}

void Put16(Byte* data, unsigned value)
{
    data[0] = Byte(value);
    data[1] = Byte(value >> 8);
}

void CheckDtd()
{
    const Timing progressive = DecodeDtd(Progressive);
    assert(progressive.IsValid() && progressive.Width() == 1920 && progressive.Height() == 1080);
    assert(progressive.clockHz == 148500000 && progressive.LineRate() == 67500 && progressive.Rate() == 60);
    assert(progressive.horizontal.Total() == 2200 && progressive.vertical.Total() == 1125);
    assert(progressive.horizontal.front == 88 && progressive.horizontal.sync == 44);
    assert(progressive.horizontal.Back() == 148 && progressive.vertical.Back() == 36);
    assert(progressive.positiveH && progressive.positiveV);

    Byte interlaced[18];
    std::copy_n(Progressive, 18, interlaced);
    Put16(interlaced, 7425);
    interlaced[5] = 28; interlaced[6] = 22; interlaced[7] = 0x20;
    interlaced[10] = 0x25; interlaced[17] |= 0x80;
    const Timing fields = DecodeDtd(interlaced);
    assert(fields.IsValid() && fields.interlaced && fields.Height() == 1080);
    assert(fields.vertical.Total() == 562.5 && fields.Rate() == 60);
    interlaced[8] = 255;
    assert(!DecodeDtd(interlaced).IsValid());

    Block base{};
    base[18] = 1; base[19] = 4; base[20] = 0x80;
    base[35] = 0x20; base[38] = 209; base[39] = 0xc0;
    const Byte range[] = {0,0,0,0xfd,2,48,30,30,160,34};
    std::copy(std::begin(range), std::end(range), base.begin() + 90);
    std::ostringstream out;
    PrintBaseDetails(out, base.data(), {progressive, fields});
    const auto report = out.str();
    assert(report.find("Established timing: 640x480p") != report.npos);
    assert(report.find("Standard timing: 1920x1080") != report.npos);
    assert(report.find("48-285 Hz vertical, 30-160 kHz horizontal") != report.npos);
    assert(report.find("Maximum pixel clock: 340 MHz") != report.npos);
    assert(report.find("Base DTD 2  1920 x 1080i @ 60.000 Hz (field rate)") != report.npos);
    assert(report.find("Vertical (lines/field)") != report.npos);
    assert(report.find("Vertical (lines/frame)") != report.npos);
    assert(std::regex_search(report, std::regex("Front porch +88 +4\n")));
}

Block MakeCta()
{
    Block block{};
    const Byte header[] = {
        2, 3, 25, 0x32,
        0x43, 144, 193, 220,        // Video data block
        0xe2, 0x0f, 1,             // YCbCr 4:2:0 capability map
        0xe2, 0x0e, 97,            // YCbCr 4:2:0-only video
        0xe3, 5, 0x80, 0,          // BT.2020 RGB
        0xe6, 6, 0x0c, 1, 32, 16, 128 // HDR static metadata
    };
    std::copy(std::begin(header), std::end(header), block.begin());
    std::copy(std::begin(Progressive), std::end(Progressive), block.begin() + block[2]);
    Seal(block.data(), block.size());
    return block;
}

Block MakeDisplayId(bool typeVII, bool interlaced)
{
    Block block{};
    block[0] = 0x70;
    block[1] = typeVII ? 0x20 : 0x13;
    const int stride = typeVII ? 21 : 20;
    block[2] = Byte(stride + 7);
    block[5] = typeVII ? 0x22 : 0x03;
    block[6] = typeVII ? 0x10 : 0;
    block[7] = Byte(stride);
    Byte* d = block.data() + 8;
    const unsigned clock = (typeVII ? 148500 : 14850) - 1;
    Put16(d, clock);
    d[2] = Byte(clock >> 16);
    d[3] = interlaced ? 0x94 : 0x84;
    Put16(d + 4, 1919);
    Put16(d + 6, 279);
    Put16(d + 8, 0x8000 | 87);
    Put16(d + 10, 43);
    Put16(d + 12, 1079);
    Put16(d + 14, 44);
    Put16(d + 16, 0x8000 | (interlaced ? 3 : 2));
    Put16(d + 18, interlaced ? 9 : 4);
    d[stride] = 0x7e;
    d[stride + 2] = 1;
    Seal(block.data() + 1, block[2] + 5);
    Seal(block.data(), block.size());
    return block;
}

void CheckExtensions()
{
    const Block cta = MakeCta();
    const Block typeI = MakeDisplayId(false, true);
    const Block typeVII = MakeDisplayId(true, false);
    const auto timing = DecodeDisplayIdTiming(typeI.data() + 8, false, 0);
    assert(timing.IsValid() && timing.interlaced && timing.preferred);
    assert(timing.Height() == 1080 && timing.vertical.Total() == 562.5 && timing.Rate() == 120);
    const auto newer = DecodeDisplayIdTiming(typeVII.data() + 8, true, 2);
    assert(newer.IsValid() && newer.Rate() == 60 && !newer.preferred);

    Block badCta = cta;
    badCta[4] = 0x5f;
    Seal(badCta.data(), badCta.size());
    Block badDisplayId = typeVII;
    badDisplayId[7] = 42;
    Seal(badDisplayId.data() + 1, badDisplayId[2] + 5);
    Seal(badDisplayId.data(), badDisplayId.size());
    Block badChecksum = cta;
    ++badChecksum.back();
    Block badInnerChecksum = typeI;
    ++badInnerChecksum[5 + badInnerChecksum[2]];
    Seal(badInnerChecksum.data(), badInnerChecksum.size());

    std::vector<Byte> edid(BlockSize);
    edid[126] = 8;
    const Block* extensions[] = {&badChecksum, &badCta, &badDisplayId, &badInnerChecksum, &cta, &typeI, &typeVII};
    for (const Block* block : extensions)
        edid.insert(edid.end(), block->begin(), block->end());
    std::ostringstream out, errors;
    const int nativeCount = PrintExtensions(out, errors, "Fixture", edid, 1);
    assert(nativeCount == 2);
    const auto report = out.str();
    const auto warnings = errors.str();
    assert(report.find("[CTA-861 / Extension 5]") != report.npos);
    assert(std::regex_search(report, std::regex("16 +1920 x 1080p +60 Hz +Native; YCbCr 4:2:0 supported")));
    assert(std::regex_search(report, std::regex("193 +5120 x 2160p +120 Hz")));
    assert(std::regex_search(report, std::regex("220 +- +-")));
    assert(std::regex_search(report, std::regex("97 +3840 x 2160p +60 Hz +YCbCr 4:2:0 only")));
    assert(report.find("CTA DTD 1  1920 x 1080p @ 60.000 Hz [native]") != report.npos);
    assert(report.find("BT.2020 RGB") != report.npos);
    assert(report.find("PQ (ST 2084) HLG") != report.npos);
    assert(report.find("Desired max luminance: 100 cd/m^2") != report.npos);
    assert(report.find("Type I DTD 1  1920 x 1080i @ 120.000 Hz (field rate) [preferred]") != report.npos);
    assert(report.find("Type VII DTD 1  1920 x 1080p @ 60.000 Hz [preferred]") != report.npos);
    assert(report.find("Data block (tag 0x7e, 1 bytes)") != report.npos);
    assert(report.find("/ Extension 1]") == report.npos);
    assert(warnings.find("block 1: invalid tag or checksum") != warnings.npos);
    assert(warnings.find("block 2: malformed payload") != warnings.npos);
    assert(warnings.find("block 3: malformed payload") != warnings.npos);
    assert(warnings.find("block 4: malformed payload") != warnings.npos);
    assert(warnings.find("block 8: missing or truncated") != warnings.npos);
}

int main()
{
    CheckDtd();
    CheckExtensions();
    std::cout << "EDID detail checks passed\n";
}
