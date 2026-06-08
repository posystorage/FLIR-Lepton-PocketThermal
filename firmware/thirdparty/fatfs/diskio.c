/* Low level disk I/O module for SD card via SDH1
 *  Adapted for FatFs R0.16 + M480 SDH BSP
 */

#include "ff.h"
#include "diskio.h"
#include "sdcard.h"
#include <string.h>

#define SD_DRIVE    0

/* Alignment buffer — SDH DMA requires 4-byte aligned buffers */
static uint32_t g_align_buf[128];   /* 512 bytes */


DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != SD_DRIVE) return STA_NOINIT;
    if (SDH_GET_CARD_CAPACITY(SDH1) == 0) return STA_NOINIT;
    return 0;
}


DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SD_DRIVE) return STA_NOINIT;

    if (!SDCard_IsInserted()) return STA_NODISK;
    if (SDH_GET_CARD_CAPACITY(SDH1) == 0) return STA_NOINIT;
    return 0;
}


DRESULT disk_read(
    BYTE pdrv,
    BYTE *buff,
    LBA_t sector,
    UINT count)
{
    const sdcard_info_t *info;
    uint32_t result;
    uint32_t sec_size;

    if (pdrv != SD_DRIVE) return RES_PARERR;
    if (!SDCard_IsReady()) return RES_NOTRDY;

    info = SDCard_GetInfo();
    sec_size = info->sector_size;

    if ((uint32_t)buff & 3) {
        if (count == 1) {
            result = SDCard_ReadBlocks((uint8_t *)g_align_buf, (uint32_t)sector, 1);
            if (result != SDCARD_OK) return RES_ERROR;
            memcpy(buff, g_align_buf, sec_size);
        } else {
            uint32_t aligned_addr = (((uint32_t)buff + 3) / 4) * 4;
            result = SDCard_ReadBlocks((uint8_t *)aligned_addr, (uint32_t)sector, count - 1);
            if (result != SDCARD_OK) return RES_ERROR;
            memcpy(buff, (void *)aligned_addr, sec_size * (count - 1));
            result = SDCard_ReadBlocks((uint8_t *)g_align_buf, (uint32_t)(sector + count - 1), 1);
            if (result != SDCARD_OK) return RES_ERROR;
            memcpy(buff + sec_size * (count - 1), g_align_buf, sec_size);
        }
        return RES_OK;
    }

    result = SDCard_ReadBlocks(buff, (uint32_t)sector, count);
    if (result != SDCARD_OK) return RES_ERROR;
    return RES_OK;
}


#if FF_FS_READONLY == 0

DRESULT disk_write(
    BYTE pdrv,
    const BYTE *buff,
    LBA_t sector,
    UINT count)
{
    const sdcard_info_t *info;
    uint32_t result;
    uint32_t sec_size;
    uint32_t i;

    if (pdrv != SD_DRIVE) return RES_PARERR;
    if (!SDCard_IsReady()) return RES_NOTRDY;

    info = SDCard_GetInfo();
    sec_size = info->sector_size;

    if ((uint32_t)buff & 3) {
        if (count == 1) {
            memcpy(g_align_buf, buff, sec_size);
            result = SDCard_WriteBlocks((uint8_t *)g_align_buf, (uint32_t)sector, 1);
        } else {
            uint32_t aligned_addr = (((uint32_t)buff + 3) / 4) * 4;
            memcpy(g_align_buf, buff + sec_size * (count - 1), sec_size);
            for (i = sec_size * (count - 1); i > 0; i--) {
                *(uint8_t *)(aligned_addr + i - 1) = buff[i - 1];
            }
            result = SDCard_WriteBlocks((uint8_t *)aligned_addr, (uint32_t)sector, count - 1);
            if (result != SDCARD_OK) return RES_ERROR;
            result = SDCard_WriteBlocks((uint8_t *)g_align_buf, (uint32_t)(sector + count - 1), 1);
        }
        if (result != SDCARD_OK) return RES_ERROR;
        return RES_OK;
    }

    result = SDCard_WriteBlocks((uint8_t *)buff, (uint32_t)sector, count);
    if (result != SDCARD_OK) return RES_ERROR;
    return RES_OK;
}

#endif


DRESULT disk_ioctl(
    BYTE pdrv,
    BYTE cmd,
    void *buff)
{
    const sdcard_info_t *info;

    if (pdrv != SD_DRIVE) return RES_PARERR;

    info = SDCard_GetInfo();

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_COUNT:
        *(DWORD *)buff = info->total_sectors;
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = info->sector_size;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
