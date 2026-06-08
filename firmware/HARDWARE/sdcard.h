#ifndef _SDCARD_H
#define _SDCARD_H
#include "M480.h"

/* SD卡类型 */
#define SDCARD_TYPE_UNKNOWN  0
#define SDCARD_TYPE_SD       1
#define SDCARD_TYPE_SDHC     2
#define SDCARD_TYPE_MMC      3

/* 返回状态 */
#define SDCARD_OK            0
#define SDCARD_ERR_INIT      1
#define SDCARD_ERR_NO_CARD   2
#define SDCARD_ERR_READ      3
#define SDCARD_ERR_WRITE     4
#define SDCARD_ERR_PARAM     5
#define SDCARD_BUSY          6

/* SD卡信息 */
typedef struct {
    uint32_t card_type;
    uint32_t total_sectors;
    uint32_t disk_size_kb;
    uint16_t sector_size;
    uint8_t  inserted;
} sdcard_info_t;

/* ── API ── */
void SDCard_Init(void);
void SDCard_Service(void);
uint32_t SDCard_ReadBlocks(uint8_t *buf, uint32_t start_sector, uint32_t count);
uint32_t SDCard_WriteBlocks(const uint8_t *buf, uint32_t start_sector, uint32_t count);
uint32_t SDCard_BeginReadBlocks(uint8_t *buf, uint32_t start_sector, uint32_t count);
uint32_t SDCard_BeginWriteBlocks(const uint8_t *buf, uint32_t start_sector, uint32_t count);
uint32_t SDCard_PollTransfer(void);
void SDCard_AbortTransfer(void);
const sdcard_info_t* SDCard_GetInfo(void);
uint8_t SDCard_IsInserted(void);

/**
 * @brief 检查 SD 卡物理层是否已初始化完毕
 * @retval 1: 卡已初始化，可进行块读写 (SDH_Read/Write)
 * @note   返回 1 仅保证块设备层可用，文件系统可能尚未挂载
 * @see    SDCard_IsMounted()
 */
uint8_t SDCard_IsReady(void);

/**
 * @brief 检查 FATFS 文件系统是否已挂载
 * @retval 1: 文件系统已挂载，可进行文件操作 (f_open/f_read等)
 * @note   SDCard_IsReady() 先返回 1 后，文件系统可能仍在挂载中
 */
uint8_t SDCard_IsMounted(void);
void SDCard_AcquireForMSC(void);
void SDCard_ReleaseFromMSC(void);
uint8_t SDCard_IsOwnedByMSC(void);

#endif
