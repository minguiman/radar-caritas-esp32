#include "ExpanderSDFS.h"

#include "vfs_api.h"
#include "ff.h"

namespace expander_sd
{
FileSystem::FileSystem() : FS(fs::FSImplPtr(new VFSImpl()))
{
}

bool FileSystem::begin(const ChipSelectControl& chipSelect,
                       SPIClass& spi,
                       uint32_t frequency,
                       const char* mountpoint,
                       uint8_t max_files,
                       bool format_if_empty)
{
    if (m_pdrv != 0xFF) {
        return true;
    }

    m_pdrv = sdcard_init(chipSelect, &spi, frequency);
    if (m_pdrv == 0xFF) {
        return false;
    }

    if (!sdcard_mount(m_pdrv, mountpoint, max_files, format_if_empty)) {
        sdcard_unmount(m_pdrv);
        sdcard_uninit(m_pdrv);
        m_pdrv = 0xFF;
        return false;
    }

    _impl->mountpoint(mountpoint);
    return true;
}

void FileSystem::end()
{
    if (m_pdrv != 0xFF) {
        _impl->mountpoint(nullptr);
        sdcard_unmount(m_pdrv);
        sdcard_uninit(m_pdrv);
        m_pdrv = 0xFF;
    }
}

sdcard_type_t FileSystem::cardType() const
{
    return (m_pdrv == 0xFF) ? CARD_NONE : sdcard_type(m_pdrv);
}

uint64_t FileSystem::cardSize() const
{
    if (m_pdrv == 0xFF) {
        return 0;
    }
    return static_cast<uint64_t>(sdcard_num_sectors(m_pdrv)) * sdcard_sector_size(m_pdrv);
}

size_t FileSystem::numSectors() const
{
    return (m_pdrv == 0xFF) ? 0 : sdcard_num_sectors(m_pdrv);
}

size_t FileSystem::sectorSize() const
{
    return (m_pdrv == 0xFF) ? 0 : sdcard_sector_size(m_pdrv);
}

uint64_t FileSystem::totalBytes() const
{
    FATFS* fsinfo = nullptr;
    DWORD freeClusters = 0;
    char drv[3] = {static_cast<char>(48 + m_pdrv), ':', 0};
    if (m_pdrv == 0xFF || f_getfree(drv, &freeClusters, &fsinfo) != 0) {
        return 0;
    }

    uint64_t size = static_cast<uint64_t>(fsinfo->csize) * (fsinfo->n_fatent - 2);
#if _MAX_SS != 512
    size *= fsinfo->ssize;
#else
    size *= 512;
#endif
    return size;
}

uint64_t FileSystem::usedBytes() const
{
    FATFS* fsinfo = nullptr;
    DWORD freeClusters = 0;
    char drv[3] = {static_cast<char>(48 + m_pdrv), ':', 0};
    if (m_pdrv == 0xFF || f_getfree(drv, &freeClusters, &fsinfo) != 0) {
        return 0;
    }

    uint64_t size = static_cast<uint64_t>(fsinfo->csize) * ((fsinfo->n_fatent - 2) - fsinfo->free_clst);
#if _MAX_SS != 512
    size *= fsinfo->ssize;
#else
    size *= 512;
#endif
    return size;
}

bool FileSystem::readRAW(uint8_t* buffer, uint32_t sector) const
{
    return (m_pdrv != 0xFF) && sd_read_raw(m_pdrv, buffer, sector);
}

bool FileSystem::writeRAW(uint8_t* buffer, uint32_t sector) const
{
    return (m_pdrv != 0xFF) && sd_write_raw(m_pdrv, buffer, sector);
}
}
