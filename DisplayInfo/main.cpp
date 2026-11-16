#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>

#include "EdidDetails.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")

using edid::BlockSize;
using edid::Read16;
constexpr size_t MaxEdidSize = 256 * BlockSize;

std::uint32_t ReadSerial(const BYTE* bytes)
{
    return Read16(bytes) | (std::uint32_t(Read16(bytes + 2)) << 16);
}

bool HasValidBase(const std::vector<BYTE>& edid)
{
    constexpr BYTE header[] = { 0, 255, 255, 255, 255, 255, 255, 0 };
    return edid.size() >= BlockSize &&
        std::equal(header, header + sizeof(header), edid.begin()) &&
        edid[18] == 1 && edid[19] <= 4 && edid::HasValidChecksum(edid.data());
}

bool ApplyOverride(std::vector<BYTE>& edid, unsigned index, const std::vector<BYTE>& block)
{
    if (index >= 256 || block.size() != BlockSize)
        return false;

    const size_t end = (size_t(index) + 1) * BlockSize;
    if (edid.size() < end)
        edid.resize(end);
    std::copy(block.begin(), block.end(), edid.begin() + end - BlockSize);
    return true;
}

LSTATUS ReadBinary(HKEY key, const wchar_t* name, std::vector<BYTE>& result, DWORD limit)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD size = 0;
        DWORD type = 0;
        LSTATUS status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
        if (status != ERROR_SUCCESS)
            return status;
        if (type != REG_BINARY)
            return ERROR_DATATYPE_MISMATCH;
        if (size > limit)
            return ERROR_INVALID_DATA;

        // Supply a non-null buffer even when the first query reported zero bytes.
        std::vector<BYTE> bytes((std::max)(size, DWORD{1}));
        status = RegQueryValueExW(key, name, nullptr, &type, bytes.data(), &size);
        if (status == ERROR_MORE_DATA)
            continue;
        if (status != ERROR_SUCCESS)
            return status;
        if (type != REG_BINARY)
            return ERROR_DATATYPE_MISMATCH;
        bytes.resize(size);
        result.swap(bytes);
        return ERROR_SUCCESS;
    }
    return ERROR_MORE_DATA;
}

void Report(const std::string& monitor, const char* message, DWORD code)
{
    std::cerr << monitor << ": " << message << " (Windows error " << code << ")\n";
}

bool ApplyRegistryOverrides(HKEY key, const std::string& monitor, std::vector<BYTE>& edid)
{
    HKEY overrides;
    const LSTATUS openStatus = RegOpenKeyExW(key, L"EDID_OVERRIDE", 0, KEY_QUERY_VALUE, &overrides);
    if (openStatus == ERROR_FILE_NOT_FOUND)
        return true;
    if (openStatus != ERROR_SUCCESS) {
        Report(monitor, "Cannot open EDID_OVERRIDE", openStatus);
        return false;
    }

    bool baseOverrideValid = true;
    for (unsigned index = 0; index < 256; ++index) {
        const auto name = std::to_wstring(index);
        std::vector<BYTE> block;
        const LSTATUS status = ReadBinary(overrides, name.c_str(), block, BlockSize);
        if (status == ERROR_FILE_NOT_FOUND)
            continue;
        if (status != ERROR_SUCCESS || !ApplyOverride(edid, index, block)) {
            std::cerr << monitor << ": EDID_OVERRIDE block " << index
                << " is unreadable or invalid (Windows error "
                << (status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status) << ")\n";
            if (index == 0)
                baseOverrideValid = false;
        }
    }
    RegCloseKey(overrides);
    return baseOverrideValid;
}

std::string Manufacturer(const BYTE* edid)
{
    const unsigned id = (unsigned(edid[8]) << 8) | edid[9];
    std::string name;
    for (int shift : {10, 5, 0}) {
        const unsigned letter = (id >> shift) & 31;
        if (letter < 1 || letter > 26)
            return "UNKNOWN";
        name += static_cast<char>('A' + letter - 1);
    }
    return name;
}

std::string MonitorName(const BYTE* edid)
{
    for (int offset = 54; offset <= 108; offset += 18) {
        const BYTE* descriptor = edid + offset;
        if (Read16(descriptor) || descriptor[2] || descriptor[3] != 0xfc || descriptor[4])
            continue;

        std::string name;
        for (int i = 5; i < 18 && descriptor[i] >= 32 && descriptor[i] <= 126; ++i)
            name += static_cast<char>(descriptor[i]);
        const size_t last = name.find_last_not_of(' ');
        if (last != std::string::npos)
            return name.substr(0, last + 1);
    }
    return "UNKNOWN";
}

double Coordinate(const BYTE* edid, int index)
{
    const unsigned low = (edid[25 + index / 4] >> (6 - 2 * (index % 4))) & 3;
    return ((unsigned(edid[27 + index]) << 2) | low) / 1024.0;
}

void PrintEdid(std::ostream& out, const BYTE* edid, const edid::Timing& timing)
{
    out << "=== " << MonitorName(edid) << " [" << Manufacturer(edid)
        << " / " << Read16(edid + 10) << "] ===\n";

    const auto label = [&out](const char* text) -> std::ostream& {
        return out << std::left << std::setw(25) << text << std::right;
    };
    const bool hasTiming = timing.IsValid();
    label(timing.preferred ? "Preferred mode" : "EDID timing");
    if (hasTiming)
        out << timing.Width() << " x " << timing.Height() << (timing.interlaced ? 'i' : 'p')
            << " @ " << std::fixed << std::setprecision(3) << timing.Rate() << " Hz"
            << (timing.interlaced ? " (field rate)" : "")
            << std::defaultfloat << std::setprecision(6);
    else
        out << "UNKNOWN";
    out << '\n';

    int widthMm = edid[21] * 10;
    int heightMm = edid[22] * 10;
    if (hasTiming && timing.widthMm && timing.heightMm) {
        widthMm = timing.widthMm;
        heightMm = timing.heightMm;
    }
    const bool hasSize = widthMm > 0 && heightMm > 0;
    label("Physical size");
    if (hasSize)
        out << widthMm << " x " << heightMm << " mm";
    else
        out << "UNKNOWN";
    out << '\n';

    label("EDID") << unsigned(edid[18]) << '.' << unsigned(edid[19]) << ", "
        << (edid[20] & 0x80 ? "digital" : "analog") << " input\n";
    label("Pixel pitch (estimated)");
    if (hasSize && timing.preferred)
        out << double(widthMm) / timing.Width() << " x "
            << double(heightMm) / timing.Height() << " mm";
    else
        out << "UNKNOWN";
    out << '\n';

    label("Serial number") << ReadSerial(edid + 12) << '\n';
    const int year = edid[17] + 1990;
    if (edid[19] == 4 && edid[16] == 0xff) {
        label("Model year") << year << '\n';
    } else {
        label("Manufacture year") << year << '\n';
        label("Manufacture week");
        if (edid[16] >= 1 && edid[16] <= 54)
            out << int(edid[16]);
        else
            out << "UNKNOWN";
        out << '\n';
    }

    label("Gamma");
    if (edid[23] == 0xff)
        out << "UNKNOWN";
    else
        out << std::fixed << std::setprecision(2) << (edid[23] + 100) / 100.0;

    out << "\n\n[Chromaticity]\n" << std::setw(20) << "x" << std::setw(12) << "y" << '\n'
        << std::fixed << std::setprecision(6);
    const char* labels[] = { "Red", "Green", "Blue", "White" };
    for (int i = 0; i < 4; ++i)
        out << "  " << std::left << std::setw(6) << labels[i] << std::right
            << std::setw(12) << Coordinate(edid, i * 2)
            << std::setw(12) << Coordinate(edid, i * 2 + 1) << '\n';
    out << std::defaultfloat << std::setprecision(6);
}

std::string MonitorIdentifier(HDEVINFO devices, SP_DEVINFO_DATA& device, DWORD index)
{
    char id[MAX_DEVICE_ID_LEN];
    if (SetupDiGetDeviceInstanceIdA(devices, &device, id, sizeof(id), nullptr))
        return id;
    return "Monitor #" + std::to_string(index + 1);
}

int main()
{
    const HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_MONITOR, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        Report("Monitor enumeration", "Cannot get device information", GetLastError());
        return 1;
    }

    bool displayed = false;
    DWORD enumError;
    for (DWORD index = 0; ; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices, index, &device)) {
            enumError = GetLastError();
            break;
        }
        const auto monitor = MonitorIdentifier(devices, device, index);
        const HKEY key = SetupDiOpenDevRegKey(
            devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) {
            Report(monitor, "Cannot open device registry key", GetLastError());
            continue;
        }

        std::vector<BYTE> edid;
        const LSTATUS status = ReadBinary(key, L"EDID", edid, MaxEdidSize);
        if (status != ERROR_SUCCESS)
            Report(monitor, "Cannot read EDID", status);
        const bool overridesValid = ApplyRegistryOverrides(key, monitor, edid);
        RegCloseKey(key);
        if (!overridesValid)
            continue;
        if (!HasValidBase(edid)) {
            std::cerr << monitor << ": Invalid or missing EDID base block\n";
            continue;
        }
        std::vector<edid::Timing> timings;
        for (int offset = 54; offset <= 108; offset += 18) {
            auto timing = edid::DecodeDtd(edid.data() + offset);
            if (!timing.clockHz)
                continue;
            if (!timing.IsValid()) {
                std::cerr << monitor << ": Base DTD " << (offset - 54) / 18 + 1
                    << " is invalid\n";
            }
            timing.preferred = offset == 54 && (edid[24] & 2) != 0;
            timings.push_back(timing);
        }
        const auto first = std::find_if(timings.begin(), timings.end(),
            [](const edid::Timing& timing) { return timing.IsValid(); });
        PrintEdid(std::cout, edid.data(), first == timings.end() ? edid::Timing{} : *first);
        std::ostringstream extensions;
        const int nativeCount = edid::PrintExtensions(
            extensions, std::cerr, monitor, edid, static_cast<int>(timings.size()));
        edid::PrintBaseDetails(std::cout, edid.data(), timings, nativeCount);
        std::cout << extensions.str() << '\n';
        displayed = true;
    }

    SetupDiDestroyDeviceInfoList(devices);
    if (enumError != ERROR_NO_MORE_ITEMS) {
        Report("Monitor enumeration", "Cannot enumerate devices", enumError);
        return 1;
    }
    if (!displayed)
        std::cerr << "No monitor with a valid EDID was found.\n";
    return displayed ? 0 : 1;
}
