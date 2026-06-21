#pragma once

#include <FS.h>
#include <SPI.h>
#include "ExpanderSdDiskio.h"

namespace expander_sd
{
class FileSystem : public fs::FS {
public:
    FileSystem();

    bool begin(const ChipSelectControl& chipSelect,
               SPIClass& spi,
               uint32_t frequency = 4'000'000,
               const char* mountpoint = "/sd",
               uint8_t max_files = 5,
               bool format_if_empty = false);

    void end();
    sdcard_type_t cardType() const;
    uint64_t cardSize() const;
    size_t numSectors() const;
    size_t sectorSize() const;
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;
    bool readRAW(uint8_t* buffer, uint32_t sector) const;
    bool writeRAW(uint8_t* buffer, uint32_t sector) const;

private:
    uint8_t m_pdrv = 0xFF;
};
}
