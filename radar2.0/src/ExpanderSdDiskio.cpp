// Based on the Arduino-ESP32 SD library, adapted for an SD chip-select line
// driven by the board IO expander instead of a regular GPIO.
#define ARDUINO_CORE_BUILD

#include "ExpanderSdDiskio.h"

#include "esp_system.h"

extern "C" {
#include "ff.h"
#include "diskio.h"
#if ESP_IDF_VERSION_MAJOR > 3
#include "diskio_impl.h"
#endif
#include "esp_vfs_fat.h"
char CRC7(const char* data, int length);
unsigned short CRC16(const char* data, int length);
}

namespace
{
typedef enum {
    GO_IDLE_STATE = 0,
    SEND_OP_COND = 1,
    SEND_CID = 2,
    SEND_RELATIVE_ADDR = 3,
    SEND_SWITCH_FUNC = 6,
    SEND_IF_COND = 8,
    SEND_CSD = 9,
    STOP_TRANSMISSION = 12,
    SEND_STATUS = 13,
    SET_BLOCKLEN = 16,
    READ_BLOCK_SINGLE = 17,
    READ_BLOCK_MULTIPLE = 18,
    SEND_NUM_WR_BLOCKS = 22,
    SET_WR_BLK_ERASE_COUNT = 23,
    WRITE_BLOCK_SINGLE = 24,
    WRITE_BLOCK_MULTIPLE = 25,
    APP_OP_COND = 41,
    APP_CLR_CARD_DETECT = 42,
    APP_CMD = 55,
    READ_OCR = 58,
    CRC_ON_OFF = 59
} SdCommand;

typedef struct {
    expander_sd::ChipSelectControl chipSelect;
    SPIClass* spi;
    int frequency;
    char* base_path;
    sdcard_type_t type;
    unsigned long sectors;
    bool supports_crc;
    int status;
} SdCard;

SdCard* s_cards[FF_VOLUMES] = {nullptr};

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
const char* kFatErrors[] = {
    "(0) Succeeded",
    "(1) A hard error occurred in the low level disk I/O layer",
    "(2) Assertion failed",
    "(3) The physical drive cannot work",
    "(4) Could not find the file",
    "(5) Could not find the path",
    "(6) The path name format is invalid",
    "(7) Access denied due to prohibited access or directory full",
    "(8) Access denied due to prohibited access",
    "(9) The file/directory object is invalid",
    "(10) The physical drive is write protected",
    "(11) The logical drive number is invalid",
    "(12) The volume has no work area",
    "(13) There is no valid FAT volume",
    "(14) The f_mkfs() aborted due to any problem",
    "(15) Could not get a grant to access the volume within defined period",
    "(16) The operation is rejected according to the file sharing policy",
    "(17) LFN working buffer could not be allocated",
    "(18) Number of open files > FF_FS_LOCK",
    "(19) Given parameter is invalid"
};
#endif

bool initChipSelect(SdCard* card)
{
    return card->chipSelect.init != nullptr && card->chipSelect.init(card->chipSelect.context);
}

void writeChipSelect(SdCard* card, bool high)
{
    if (card->chipSelect.write != nullptr) {
        card->chipSelect.write(card->chipSelect.context, high);
    }
}

bool sdWait(uint8_t pdrv, int timeout)
{
    char resp;
    uint32_t start = millis();
    do {
        resp = s_cards[pdrv]->spi->transfer(0xFF);
    } while (resp == 0x00 && (millis() - start) < static_cast<unsigned int>(timeout));
    return (resp > 0x00);
}

void sdStop(uint8_t pdrv)
{
    s_cards[pdrv]->spi->write(0xFD);
}

void sdDeselectCard(uint8_t pdrv)
{
    writeChipSelect(s_cards[pdrv], true);
}

bool sdSelectCard(uint8_t pdrv)
{
    SdCard* card = s_cards[pdrv];
    writeChipSelect(card, false);
    bool ready = sdWait(pdrv, 500);
    if (!ready) {
        writeChipSelect(card, true);
    }
    return ready;
}

char sdCommand(uint8_t pdrv, char cmd, unsigned int arg, unsigned int* resp)
{
    char token;
    SdCard* card = s_cards[pdrv];

    for (int f = 0; f < 3; f++) {
        if (cmd == SEND_NUM_WR_BLOCKS || cmd == SET_WR_BLK_ERASE_COUNT || cmd == APP_OP_COND || cmd == APP_CLR_CARD_DETECT) {
            token = sdCommand(pdrv, APP_CMD, 0, nullptr);
            sdDeselectCard(pdrv);
            if (token > 1) {
                break;
            }
            if (!sdSelectCard(pdrv)) {
                token = 0xFF;
                break;
            }
        }

        char cmdPacket[7];
        cmdPacket[0] = cmd | 0x40;
        cmdPacket[1] = arg >> 24;
        cmdPacket[2] = arg >> 16;
        cmdPacket[3] = arg >> 8;
        cmdPacket[4] = arg;
        if (card->supports_crc || cmd == GO_IDLE_STATE || cmd == SEND_IF_COND) {
            cmdPacket[5] = (CRC7(cmdPacket, 5) << 1) | 0x01;
        } else {
            cmdPacket[5] = 0x01;
        }
        cmdPacket[6] = 0xFF;

        card->spi->writeBytes(reinterpret_cast<uint8_t*>(cmdPacket), (cmd == STOP_TRANSMISSION) ? 7 : 6);

        for (int i = 0; i < 9; i++) {
            token = card->spi->transfer(0xFF);
            if (!(token & 0x80)) {
                break;
            }
        }

        if (token == 0xFF) {
            sdDeselectCard(pdrv);
            delay(100);
            sdSelectCard(pdrv);
            continue;
        }
        if (token & 0x08) {
            sdDeselectCard(pdrv);
            delay(100);
            sdSelectCard(pdrv);
            continue;
        }
        if (token > 1) {
            break;
        }

        if (cmd == SEND_STATUS && resp) {
            *resp = card->spi->transfer(0xFF);
        } else if ((cmd == SEND_IF_COND || cmd == READ_OCR) && resp) {
            *resp = card->spi->transfer32(0xFFFFFFFF);
        }
        break;
    }

    if (token == 0xFF) {
        card->status = STA_NOINIT;
    }
    return token;
}

bool sdReadBytes(uint8_t pdrv, char* buffer, int length)
{
    char token;
    unsigned short crc;
    SdCard* card = s_cards[pdrv];

    uint32_t start = millis();
    do {
        token = card->spi->transfer(0xFF);
    } while (token == 0xFF && (millis() - start) < 500);

    if (token != 0xFE) {
        return false;
    }

    card->spi->transferBytes(nullptr, reinterpret_cast<uint8_t*>(buffer), length);
    crc = card->spi->transfer16(0xFFFF);
    return (!card->supports_crc || crc == CRC16(buffer, length));
}

char sdWriteBytes(uint8_t pdrv, const char* buffer, char token)
{
    SdCard* card = s_cards[pdrv];
    unsigned short crc = card->supports_crc ? CRC16(buffer, 512) : 0xFFFF;
    if (!sdWait(pdrv, 500)) {
        return 0;
    }

    card->spi->write(token);
    card->spi->writeBytes(reinterpret_cast<const uint8_t*>(buffer), 512);
    card->spi->write16(crc);
    return (card->spi->transfer(0xFF) & 0x1F);
}

char sdTransaction(uint8_t pdrv, char cmd, unsigned int arg, unsigned int* resp)
{
    if (!sdSelectCard(pdrv)) {
        return 0xFF;
    }
    char token = sdCommand(pdrv, cmd, arg, resp);
    sdDeselectCard(pdrv);
    return token;
}

bool sdReadSector(uint8_t pdrv, char* buffer, unsigned long long sector)
{
    for (int f = 0; f < 3; f++) {
        if (!sdSelectCard(pdrv)) {
            return false;
        }
        if (!sdCommand(pdrv, READ_BLOCK_SINGLE, (s_cards[pdrv]->type == CARD_SDHC) ? sector : sector << 9, nullptr)) {
            bool success = sdReadBytes(pdrv, buffer, 512);
            sdDeselectCard(pdrv);
            if (success) {
                return true;
            }
        } else {
            break;
        }
    }
    sdDeselectCard(pdrv);
    return false;
}

bool sdReadSectors(uint8_t pdrv, char* buffer, unsigned long long sector, int count)
{
    for (int f = 0; f < 3;) {
        if (!sdSelectCard(pdrv)) {
            return false;
        }

        if (!sdCommand(pdrv, READ_BLOCK_MULTIPLE, (s_cards[pdrv]->type == CARD_SDHC) ? sector : sector << 9, nullptr)) {
            do {
                if (!sdReadBytes(pdrv, buffer, 512)) {
                    f++;
                    break;
                }
                sector++;
                buffer += 512;
                f = 0;
            } while (--count);

            if (sdCommand(pdrv, STOP_TRANSMISSION, 0, nullptr)) {
                break;
            }

            sdDeselectCard(pdrv);
            if (count == 0) {
                return true;
            }
        } else {
            break;
        }
    }
    sdDeselectCard(pdrv);
    return false;
}

bool sdWriteSector(uint8_t pdrv, const char* buffer, unsigned long long sector)
{
    for (int f = 0; f < 3; f++) {
        if (!sdSelectCard(pdrv)) {
            return false;
        }
        if (!sdCommand(pdrv, WRITE_BLOCK_SINGLE, (s_cards[pdrv]->type == CARD_SDHC) ? sector : sector << 9, nullptr)) {
            char token = sdWriteBytes(pdrv, buffer, 0xFE);
            sdDeselectCard(pdrv);

            if (token == 0x0A) {
                continue;
            }
            if (token == 0x0C) {
                return false;
            }

            unsigned int resp;
            if (sdTransaction(pdrv, SEND_STATUS, 0, &resp) || resp) {
                return false;
            }
            return true;
        }
        break;
    }
    sdDeselectCard(pdrv);
    return false;
}

bool sdWriteSectors(uint8_t pdrv, const char* buffer, unsigned long long sector, int count)
{
    char token;
    const char* currentBuffer = buffer;
    unsigned long long currentSector = sector;
    int currentCount = count;
    SdCard* card = s_cards[pdrv];

    for (int f = 0; f < 3;) {
        if (card->type != CARD_MMC) {
            if (sdTransaction(pdrv, SET_WR_BLK_ERASE_COUNT, currentCount, nullptr)) {
                return false;
            }
        }

        if (!sdSelectCard(pdrv)) {
            return false;
        }

        if (!sdCommand(pdrv, WRITE_BLOCK_MULTIPLE, (card->type == CARD_SDHC) ? currentSector : currentSector << 9, nullptr)) {
            do {
                token = sdWriteBytes(pdrv, currentBuffer, 0xFC);
                if (token != 0x05) {
                    f++;
                    break;
                }
                currentBuffer += 512;
                f = 0;
            } while (--currentCount);

            if (!sdWait(pdrv, 500)) {
                break;
            }

            if (currentCount == 0) {
                sdStop(pdrv);
                sdDeselectCard(pdrv);
                unsigned int resp;
                if (sdTransaction(pdrv, SEND_STATUS, 0, &resp) || resp) {
                    return false;
                }
                return true;
            }

            if (sdCommand(pdrv, STOP_TRANSMISSION, 0, nullptr)) {
                break;
            }

            if (token == 0x0A) {
                sdDeselectCard(pdrv);
                unsigned int writtenBlocks = 0;
                if (card->type != CARD_MMC && sdSelectCard(pdrv)) {
                    if (!sdCommand(pdrv, SEND_NUM_WR_BLOCKS, 0, nullptr)) {
                        char acmdData[4];
                        if (sdReadBytes(pdrv, acmdData, 4)) {
                            writtenBlocks = acmdData[0] << 24;
                            writtenBlocks |= acmdData[1] << 16;
                            writtenBlocks |= acmdData[2] << 8;
                            writtenBlocks |= acmdData[3];
                        }
                    }
                    sdDeselectCard(pdrv);
                }
                currentBuffer = buffer + (writtenBlocks << 9);
                currentSector = sector + writtenBlocks;
                currentCount = count - writtenBlocks;
                continue;
            }
            break;
        }
        break;
    }

    sdDeselectCard(pdrv);
    return false;
}

unsigned long sdGetSectorsCount(uint8_t pdrv)
{
    for (int f = 0; f < 3; f++) {
        if (!sdSelectCard(pdrv)) {
            return 0;
        }

        if (!sdCommand(pdrv, SEND_CSD, 0, nullptr)) {
            char csd[16];
            bool success = sdReadBytes(pdrv, csd, 16);
            sdDeselectCard(pdrv);
            if (success) {
                if ((csd[0] >> 6) == 0x01) {
                    unsigned long size = (((unsigned long)(csd[7] & 0x3F) << 16) | ((unsigned long)csd[8] << 8) | csd[9]) + 1;
                    return size << 10;
                }
                unsigned long size = (((unsigned long)(csd[6] & 0x03) << 10) | ((unsigned long)csd[7] << 2) | ((csd[8] & 0xC0) >> 6)) + 1;
                size <<= ((((csd[9] & 0x03) << 1) | ((csd[10] & 0x80) >> 7)) + 2);
                size <<= (csd[5] & 0x0F);
                return size >> 9;
            }
        } else {
            break;
        }
    }

    sdDeselectCard(pdrv);
    return 0;
}

struct AcquireSPI {
    explicit AcquireSPI(SdCard* card) : m_card(card)
    {
        m_card->spi->beginTransaction(SPISettings(m_card->frequency, MSBFIRST, SPI_MODE0));
    }

    AcquireSPI(SdCard* card, int frequency) : m_card(card)
    {
        m_card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
    }

    ~AcquireSPI()
    {
        m_card->spi->endTransaction();
    }

    SdCard* m_card;
};

DSTATUS ff_sd_initialize(uint8_t pdrv)
{
    char token;
    unsigned int resp;
    unsigned int start;
    SdCard* card = s_cards[pdrv];

    if (!(card->status & STA_NOINIT)) {
        return card->status;
    }

    AcquireSPI lock(card, 400000);

    writeChipSelect(card, true);
    for (uint8_t i = 0; i < 20; i++) {
        card->spi->transfer(0xFF);
    }

    writeChipSelect(card, false);
    if (!sdWait(pdrv, 500)) {
        // Keep going, same behaviour as the stock Arduino SD driver.
    }
    if (sdCommand(pdrv, GO_IDLE_STATE, 0, nullptr) != 1) {
        sdDeselectCard(pdrv);
        goto unknown_card;
    }
    sdDeselectCard(pdrv);

    token = sdTransaction(pdrv, CRC_ON_OFF, 1, nullptr);
    if (token == 0x5) {
        card->supports_crc = false;
    } else if (token != 1) {
        goto unknown_card;
    }

    if (sdTransaction(pdrv, SEND_IF_COND, 0x1AA, &resp) == 1) {
        if ((resp & 0xFFF) != 0x1AA) {
            goto unknown_card;
        }
        if (sdTransaction(pdrv, READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20))) {
            goto unknown_card;
        }

        start = millis();
        do {
            token = sdTransaction(pdrv, APP_OP_COND, 0x40100000, nullptr);
        } while (token == 1 && (millis() - start) < 1000);

        if (token) {
            goto unknown_card;
        }

        if (!sdTransaction(pdrv, READ_OCR, 0, &resp)) {
            card->type = (resp & (1 << 30)) ? CARD_SDHC : CARD_SD;
        } else {
            goto unknown_card;
        }
    } else {
        if (sdTransaction(pdrv, READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20))) {
            goto unknown_card;
        }

        start = millis();
        do {
            token = sdTransaction(pdrv, APP_OP_COND, 0x100000, nullptr);
        } while (token == 0x01 && (millis() - start) < 1000);

        if (!token) {
            card->type = CARD_SD;
        } else {
            start = millis();
            do {
                token = sdTransaction(pdrv, SEND_OP_COND, 0x100000, nullptr);
            } while (token != 0x00 && (millis() - start) < 1000);

            if (token == 0x00) {
                card->type = CARD_MMC;
            } else {
                goto unknown_card;
            }
        }
    }

    if (card->type != CARD_MMC) {
        if (sdTransaction(pdrv, APP_CLR_CARD_DETECT, 0, nullptr)) {
            goto unknown_card;
        }
    }

    if (card->type != CARD_SDHC) {
        if (sdTransaction(pdrv, SET_BLOCKLEN, 512, nullptr) != 0x00) {
            goto unknown_card;
        }
    }

    card->sectors = sdGetSectorsCount(pdrv);
    if (card->frequency > 25000000) {
        card->frequency = 25000000;
    }
    card->status &= ~STA_NOINIT;
    return card->status;

unknown_card:
    card->type = CARD_UNKNOWN;
    return card->status;
}

DSTATUS ff_sd_status(uint8_t pdrv)
{
    SdCard* card = s_cards[pdrv];
    AcquireSPI lock(card);
    if (sdTransaction(pdrv, SEND_STATUS, 0, nullptr)) {
        return STA_NOINIT;
    }
    return s_cards[pdrv]->status;
}

DRESULT ff_sd_read(uint8_t pdrv, uint8_t* buffer, DWORD sector, UINT count)
{
    SdCard* card = s_cards[pdrv];
    if (card->status & STA_NOINIT) {
        return RES_NOTRDY;
    }
    AcquireSPI lock(card);
    return (count > 1) ? (sdReadSectors(pdrv, reinterpret_cast<char*>(buffer), sector, count) ? RES_OK : RES_ERROR)
                       : (sdReadSector(pdrv, reinterpret_cast<char*>(buffer), sector) ? RES_OK : RES_ERROR);
}

DRESULT ff_sd_write(uint8_t pdrv, const uint8_t* buffer, DWORD sector, UINT count)
{
    SdCard* card = s_cards[pdrv];
    if (card->status & STA_NOINIT) {
        return RES_NOTRDY;
    }
    if (card->status & STA_PROTECT) {
        return RES_WRPRT;
    }
    AcquireSPI lock(card);
    return (count > 1) ? (sdWriteSectors(pdrv, reinterpret_cast<const char*>(buffer), sector, count) ? RES_OK : RES_ERROR)
                       : (sdWriteSector(pdrv, reinterpret_cast<const char*>(buffer), sector) ? RES_OK : RES_ERROR);
}

DRESULT ff_sd_ioctl(uint8_t pdrv, uint8_t cmd, void* buff)
{
    switch (cmd) {
        case CTRL_SYNC: {
            AcquireSPI lock(s_cards[pdrv]);
            if (sdSelectCard(pdrv)) {
                sdDeselectCard(pdrv);
                return RES_OK;
            }
            return RES_ERROR;
        }
        case GET_SECTOR_COUNT:
            *((unsigned long*)buff) = s_cards[pdrv]->sectors;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *((WORD*)buff) = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *((uint32_t*)buff) = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
}

namespace expander_sd
{
bool sd_read_raw(uint8_t pdrv, uint8_t* buffer, uint32_t sector)
{
    return ff_sd_read(pdrv, buffer, sector, 1) == ESP_OK;
}

bool sd_write_raw(uint8_t pdrv, uint8_t* buffer, uint32_t sector)
{
    return ff_sd_write(pdrv, buffer, sector, 1) == ESP_OK;
}

uint8_t sdcard_uninit(uint8_t pdrv)
{
    SdCard* card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == nullptr) {
        return 1;
    }

    {
        AcquireSPI lock(card);
        sdTransaction(pdrv, GO_IDLE_STATE, 0, nullptr);
    }

    ff_diskio_register(pdrv, nullptr);
    s_cards[pdrv] = nullptr;
    esp_err_t err = ESP_OK;
    if (card->base_path != nullptr) {
        err = esp_vfs_fat_unregister_path(card->base_path);
        free(card->base_path);
    }
    free(card);
    return err;
}

uint8_t sdcard_init(const ChipSelectControl& chipSelect, SPIClass* spi, int hz)
{
    uint8_t pdrv = 0xFF;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == 0xFF) {
        return pdrv;
    }

    SdCard* card = static_cast<SdCard*>(malloc(sizeof(SdCard)));
    if (card == nullptr) {
        return 0xFF;
    }

    card->chipSelect = chipSelect;
    card->base_path = nullptr;
    card->frequency = hz;
    card->spi = spi;
    card->supports_crc = true;
    card->type = CARD_NONE;
    card->status = STA_NOINIT;
    card->sectors = 0;

    if (!initChipSelect(card)) {
        free(card);
        return 0xFF;
    }
    writeChipSelect(card, true);

    s_cards[pdrv] = card;
    static const ff_diskio_impl_t sd_impl = {
        .init = &ff_sd_initialize,
        .status = &ff_sd_status,
        .read = &ff_sd_read,
        .write = &ff_sd_write,
        .ioctl = &ff_sd_ioctl,
    };
    ff_diskio_register(pdrv, &sd_impl);
    return pdrv;
}

uint8_t sdcard_unmount(uint8_t pdrv)
{
    SdCard* card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == nullptr) {
        return 1;
    }
    card->status |= STA_NOINIT;
    card->type = CARD_NONE;
    char drv[3] = {static_cast<char>('0' + pdrv), ':', 0};
    f_mount(nullptr, drv, 0);
    return 0;
}

bool sdcard_mount(uint8_t pdrv, const char* path, uint8_t max_files, bool format_if_empty)
{
    SdCard* card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == nullptr) {
        return false;
    }

    if (card->base_path != nullptr) {
        free(card->base_path);
    }
    card->base_path = strdup(path);

    FATFS* fs = nullptr;
    char drv[3] = {static_cast<char>('0' + pdrv), ':', 0};
    esp_err_t err = esp_vfs_fat_register(path, drv, max_files, &fs);
    if (err != ESP_OK) {
        return false;
    }

    FRESULT res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        if (res == 13 && format_if_empty) {
            BYTE* work = static_cast<BYTE*>(malloc(sizeof(BYTE) * FF_MAX_SS));
            if (work == nullptr) {
                esp_vfs_fat_unregister_path(path);
                return false;
            }
            const MKFS_PARM opt = {(BYTE)FM_ANY, 0, 0, 0, 0};
            res = f_mkfs(drv, &opt, work, sizeof(BYTE) * FF_MAX_SS);
            free(work);
            if (res != FR_OK) {
                esp_vfs_fat_unregister_path(path);
                return false;
            }
            res = f_mount(fs, drv, 1);
        }
        if (res != FR_OK) {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
            if (res < (sizeof(kFatErrors) / sizeof(kFatErrors[0]))) {
                log_e("f_mount failed: %s", kFatErrors[res]);
            }
#endif
            esp_vfs_fat_unregister_path(path);
            return false;
        }
    }

    AcquireSPI lock(card);
    card->sectors = sdGetSectorsCount(pdrv);
    return true;
}

uint32_t sdcard_num_sectors(uint8_t pdrv)
{
    SdCard* card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == nullptr) {
        return 0;
    }
    if ((card->status & STA_NOINIT) || card->sectors == 0) {
        if (ff_sd_initialize(pdrv) & STA_NOINIT) {
            return 0;
        }
    }
    return card->sectors;
}

uint32_t sdcard_sector_size(uint8_t pdrv)
{
    if (pdrv >= FF_VOLUMES || s_cards[pdrv] == nullptr) {
        return 0;
    }
    return 512;
}

sdcard_type_t sdcard_type(uint8_t pdrv)
{
    SdCard* card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == nullptr) {
        return CARD_NONE;
    }
    return card->type;
}
}
