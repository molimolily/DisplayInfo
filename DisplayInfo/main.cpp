#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <devguid.h>
#include <cstdlib>

#pragma comment(lib, "setupapi.lib")

// Function to get Manufacturer ID from EDID
std::string GetManufacturerID(const BYTE* edidData) {
    char manufacturerId[4] = { 0 };
    manufacturerId[0] = ((edidData[8] & 0x7C) >> 2) + 'A' - 1;
    manufacturerId[1] = (((edidData[8] & 0x03) << 3) | ((edidData[9] & 0xE0) >> 5)) + 'A' - 1;
    manufacturerId[2] = (edidData[9] & 0x1F) + 'A' - 1;
    return std::string(manufacturerId);
}

// Function to get Product Code from EDID
int GetProductCode(const BYTE* edidData) {
    return edidData[10] | (edidData[11] << 8);
}

// Function to get Serial Number from EDID
int GetSerialNumber(const BYTE* edidData) {
    return edidData[12] | (edidData[13] << 8) | (edidData[14] << 16) | (edidData[15] << 24);
}

// Function to get Manufacture Date from EDID
void GetManufactureDate(const BYTE* edidData, int& week, int& year) {
    week = edidData[16];
    year = edidData[17] + 1990; // EDID uses 1990 as base year
}

// Function to get Monitor Name from EDID
std::string GetMonitorName(const BYTE* edidData) {
    for (int i = 0; i < 4; ++i) {
        int offset = 54 + (i * 18);
        if (edidData[offset] == 0x00 && edidData[offset + 1] == 0x00 && edidData[offset + 2] == 0x00) {
            if (edidData[offset + 3] == 0xFC) { // Monitor name descriptor
                char monitorName[14] = { 0 };
                memcpy(monitorName, &edidData[offset + 5], 13);
                // Trim trailing spaces
                for (int j = 12; j >= 0; --j) {
                    if (monitorName[j] == ' ' || monitorName[j] == '\n' || monitorName[j] == '\r') {
                        monitorName[j] = '\0';
                    }
                    else {
                        break;
                    }
                }
                return std::string(monitorName);
            }
        }
    }
    return "Unknown";
}

// Function to get physical size from EDID
bool GetPhysicalSizeFromEDID(const BYTE* edidData, int& widthMm, int& heightMm) {
    if (!edidData) return false;

    widthMm = edidData[21];
    heightMm = edidData[22];

    if (widthMm == 0 || heightMm == 0) return false;

    widthMm *= 10;
    heightMm *= 10;

    return true;
}

// Function to get native resolution from EDID
bool GetNativeResolutionFromEDID(const BYTE* edidData, int& horizontalResolution, int& verticalResolution) {
    if (!edidData) return false;

    // Detailed Timing Descriptor (first block)
    int offset = 54;

    // Pixel clock
    int pixelClock = edidData[offset] | (edidData[offset + 1] << 8);
    if (pixelClock == 0) {
        return false;
    }

    // Horizontal resolution
    int hActive = ((edidData[offset + 4] & 0xF0) << 4) | edidData[offset + 2];
    // Vertical resolution
    int vActive = ((edidData[offset + 7] & 0xF0) << 4) | edidData[offset + 5];

    horizontalResolution = hActive;
    verticalResolution = vActive;

    return true;
}

// Function to get gamma from EDID
bool GetGammaFromEDID(const BYTE* edidData, double& gamma) {
    if (!edidData) return false;

    BYTE gammaByte = edidData[23];
    if (gammaByte == 0 || gammaByte == 0xFF) {
        return false;
    }

    gamma = (gammaByte + 100) / 100.0;
    return true;
}

// Function to get chromaticity coordinates from EDID
bool GetChromaticityCoordinates(const BYTE* edidData, double& redX, double& redY, double& greenX, double& greenY, double& blueX, double& blueY, double& whiteX, double& whiteY) {
    if (!edidData) return false;

    // Bits [9:8] of each coordinate
    BYTE rxH = (edidData[25] & 0xC0) >> 6;
    BYTE ryH = (edidData[25] & 0x30) >> 4;
    BYTE gxH = (edidData[25] & 0x0C) >> 2;
    BYTE gyH = (edidData[25] & 0x03);
    BYTE bxH = (edidData[26] & 0xC0) >> 6;
    BYTE byH = (edidData[26] & 0x30) >> 4;
    BYTE wxH = (edidData[26] & 0x0C) >> 2;
    BYTE wyH = (edidData[26] & 0x03);

    // Bits [7:0] of each coordinate
    BYTE rxL = edidData[27];
    BYTE ryL = edidData[28];
    BYTE gxL = edidData[29];
    BYTE gyL = edidData[30];
    BYTE bxL = edidData[31];
    BYTE byL = edidData[32];
    BYTE wxL = edidData[33];
    BYTE wyL = edidData[34];

    // Combine high and low bits
    redX = ((rxH << 8) | rxL) / 1024.0;
    redY = ((ryH << 8) | ryL) / 1024.0;
    greenX = ((gxH << 8) | gxL) / 1024.0;
    greenY = ((gyH << 8) | gyL) / 1024.0;
    blueX = ((bxH << 8) | bxL) / 1024.0;
    blueY = ((byH << 8) | byL) / 1024.0;
    whiteX = ((wxH << 8) | wxL) / 1024.0;
    whiteY = ((wyH << 8) | wyL) / 1024.0;

    return true;
}

// Function to get refresh rate from EDID
bool GetRefreshRateFromEDID(const BYTE* edidData, double& refreshRate) {
    if (!edidData) return false;

    // Detailed Timing Descriptor (first block)
    int offset = 54;

    // Pixel clock in kHz (needs to be multiplied by 10 to get Hz)
    int pixelClock = (edidData[offset] | (edidData[offset + 1] << 8)) * 10;
    if (pixelClock == 0) {
        return false;
    }

    // Horizontal active pixels and blanking
    int hActive = ((edidData[offset + 4] & 0xF0) << 4) | edidData[offset + 2];
    int hBlank = ((edidData[offset + 4] & 0x0F) << 8) | edidData[offset + 3];
    int hTotal = hActive + hBlank;

    // Vertical active lines and blanking
    int vActive = ((edidData[offset + 7] & 0xF0) << 4) | edidData[offset + 5];
    int vBlank = ((edidData[offset + 7] & 0x0F) << 8) | edidData[offset + 6];
    int vTotal = vActive + vBlank;

    // Refresh rate calculation
    refreshRate = (pixelClock * 1000.0) / (hTotal * vTotal);
    return true;
}

int main() {
    HDEVINFO hDevInfo = SetupDiGetClassDevsEx(
        &GUID_DEVCLASS_MONITOR,
        NULL,
        NULL,
        DIGCF_PRESENT,
        NULL,
        NULL,
        NULL
    );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to get device information." << std::endl;
        return -1;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    DWORD deviceIndex = 0;
    while (SetupDiEnumDeviceInfo(hDevInfo, deviceIndex, &devInfoData)) {
        deviceIndex++;

        HKEY hDevRegKey = SetupDiOpenDevRegKey(
            hDevInfo,
            &devInfoData,
            DICS_FLAG_GLOBAL,
            0,
            DIREG_DEV,
            KEY_READ
        );

        if (hDevRegKey == INVALID_HANDLE_VALUE) {
            continue;
        }

        BYTE edidData[1024] = { 0 };
        DWORD edidSize = sizeof(edidData);

        DWORD regType = 0;
        LONG retValue = RegQueryValueExA(
            hDevRegKey,
            "EDID",
            NULL,
            &regType,
            edidData,
            &edidSize
        );

        RegCloseKey(hDevRegKey);

        if (retValue != ERROR_SUCCESS || edidSize == 0) {
            continue;
        }

        // Get various information from EDID
        std::string manufacturerId = GetManufacturerID(edidData);
        int productCode = GetProductCode(edidData);
        int serialNumber = GetSerialNumber(edidData);
        int week = 0, year = 0;
        GetManufactureDate(edidData, week, year);
        std::string monitorName = GetMonitorName(edidData);

        // Display the information
        std::cout << "Monitor Name: " << monitorName << std::endl;
        std::cout << "Manufacturer ID: " << manufacturerId << std::endl;
        std::cout << "Product Code: " << productCode << std::endl;
        std::cout << "Serial Number: " << serialNumber << std::endl;
        std::cout << "Manufacture Week: " << week << std::endl;
        std::cout << "Manufacture Year: " << year << std::endl;

        int nativeWidth = 0, nativeHeight = 0;
        if (GetNativeResolutionFromEDID(edidData, nativeWidth, nativeHeight)) {
            std::cout << "Native Resolution: " << nativeWidth << "x" << nativeHeight << std::endl;
        }
        else {
			std::cout << "Native Resolution: UNKNOWN " << std::endl;
		}

        int widthMm = 0, heightMm = 0;
        if (GetPhysicalSizeFromEDID(edidData, widthMm, heightMm)) {
            std::cout << "Physical Size: " << widthMm << " mm x " << heightMm << " mm" << std::endl;

            double pixelPitchWidth = static_cast<double>(widthMm) / nativeWidth;
            double pixelPitchHeight = static_cast<double>(heightMm) / nativeHeight;
            std::cout << "Pixel Pitch: " << pixelPitchWidth << " mm x " << pixelPitchHeight << " mm" << std::endl;
        }
        else {
			std::cout << "Physical Size: UNKNOWN" << std::endl;
            std::cout << "Pixel Pitch: UNKNOWN" << std::endl;
		}

        double refreshRate = 0.0;
        if (GetRefreshRateFromEDID(edidData, refreshRate)) {
            std::cout << "Refresh Rate: " << refreshRate << " Hz" << std::endl;
        }
        else {
			std::cout << "Refresh Rate: UNKNOWN" << std::endl;
		}

        double gamma = 0.0;
        if (GetGammaFromEDID(edidData, gamma)) {
            std::cout << "Gamma: " << gamma << std::endl;
        }
		else {
            std::cout << "Gamma: UNKNOWN" << std::endl;
        }

        double redX, redY, greenX, greenY, blueX, blueY, whiteX, whiteY;
        if (GetChromaticityCoordinates(edidData, redX, redY, greenX, greenY, blueX, blueY, whiteX, whiteY)) {
            std::cout << "Chromaticity Coordinates:" << std::endl;
            std::cout << "  Red   x=" << redX << ", y=" << redY << std::endl;
            std::cout << "  Green x=" << greenX << ", y=" << greenY << std::endl;
            std::cout << "  Blue  x=" << blueX << ", y=" << blueY << std::endl;
            std::cout << "  White x=" << whiteX << ", y=" << whiteY << std::endl;
        }
        else {
			std::cout << "Chromaticity Coordinates: UNKNOWN" << std::endl;
		}

        std::cout << "----------------------------------------" << std::endl;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    system("pause");

    return 0;
}
