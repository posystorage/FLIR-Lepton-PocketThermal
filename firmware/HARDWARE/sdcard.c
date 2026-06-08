#include "sdcard.h"
#include "sdh.h"
#include "sys.h"
#include "clk.h"
#include "gpio.h"
#include "sys_tick.h"
#include "debug.h"
#include <string.h>
#include "ff.h"

static FATFS g_fatfs;

/* ── SDH1 引脚: PG9~PG15, MFP3 ── */

/* ── SD 状态机 ── */
typedef enum {
    SD_STATE_NO_CARD,       /* 无卡 / 等待插入 */
    SD_STATE_INIT,          /* SDH_Probe 物理初始化 */
    SD_STATE_FS_MOUNT,      /* f_mount 挂载文件系统 (块设备已就绪) */
    SD_STATE_READY,         /* 正常运行，文件系统已挂载 */
    SD_STATE_ERROR,         /* 初始化失败，等待卡事件重试 */
} sd_state_t;

static sd_state_t     g_sd_state = SD_STATE_NO_CARD;
static sdcard_info_t  g_sd_info;
static volatile uint8_t g_sd_card_changed = 0;
static uint32_t       g_sd_poll_tick = 0;
static uint32_t       g_sd_init_tick = 0;
static uint8_t        g_sd_msc_owned = 0;
static uint8_t        g_sd_async_op = 0;
static volatile uint8_t g_sd_async_error = 0;

#define SD_POLL_INTERVAL_MS   2000
#define SD_INIT_DELAY_MS      250     /* 卡检测后等待稳定再开始 SDH_Probe */
#define SD_ASYNC_READ          1
#define SD_ASYNC_WRITE         2

extern uint32_t SDH_SDCmdAndRsp(SDH_T *sdh, uint32_t cmd, uint32_t arg, uint32_t timeout);
extern uint32_t SDH_SDCommand(SDH_T *sdh, uint32_t cmd, uint32_t arg);
extern void SDH_CheckRB(SDH_T *sdh);

/* ── 主机和外设初始化 (仅配置引脚和时钟, 不探测卡) ── */
void SDCard_Init(void)
{
    memset(&g_sd_info, 0, sizeof(g_sd_info));

    SYS_UnlockReg();

    /* 配置 SDH1 引脚 MFP */
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG14MFP_Msk) | SYS_GPG_MFPH_PG14MFP_SD1_CLK;
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG13MFP_Msk) | SYS_GPG_MFPH_PG13MFP_SD1_CMD;
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG12MFP_Msk) | SYS_GPG_MFPH_PG12MFP_SD1_DAT0;
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG11MFP_Msk) | SYS_GPG_MFPH_PG11MFP_SD1_DAT1;
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG10MFP_Msk) | SYS_GPG_MFPH_PG10MFP_SD1_DAT2;
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG9MFP_Msk)  | SYS_GPG_MFPH_PG9MFP_SD1_DAT3;
    SYS->GPG_MFPH = (SYS->GPG_MFPH & ~SYS_GPG_MFPH_PG15MFP_Msk) | SYS_GPG_MFPH_PG15MFP_SD1_nCD;

    /* 参考代码方式: 使用 CLK_SetModuleClock 配置时钟源和分频 */
    CLK_EnableModuleClock(SDH1_MODULE);
    CLK_SetModuleClock(SDH1_MODULE, CLK_CLKSEL0_SDH1SEL_PLL, CLK_CLKDIV3_SDH1(10));

    SYS_LockReg();

    /* 打开 SD 主机控制器 (使能卡检测中断) */
    SDH_Open(SDH1, CardDetect_From_GPIO);

    g_sd_state = SD_STATE_NO_CARD;
    g_sd_card_changed = 0;
    g_sd_poll_tick = GetTick();
    g_sd_msc_owned = 0;
    g_sd_async_op = 0;
    g_sd_async_error = 0;
}

/* ── 状态机服务 (主循环中调用) ── */
void SDCard_Service(void)
{
    uint8_t changed;
    uint32_t ret;

    changed = g_sd_card_changed;
    g_sd_card_changed = 0;

    switch (g_sd_state) {

    case SD_STATE_NO_CARD:
        /* 中断事件触发立即检测, 否则每 2000ms 轮询一次 */
        if (!changed) {
            if ((int32_t)(GetTick() - g_sd_poll_tick) < SD_POLL_INTERVAL_MS)
                return;
        }
        g_sd_poll_tick = GetTick();

        if (!SDH_CardDetection(SDH1))
            return;

        SD_DEBUG("card detected, starting init");
        g_sd_init_tick = GetTick();
        g_sd_state = SD_STATE_INIT;
        break;

    case SD_STATE_INIT:
        /* 等待卡信号稳定后再开始 SDH_Probe，避免上电/插入时太快导致卡死 */
        if ((int32_t)(GetTick() - g_sd_init_tick) < SD_INIT_DELAY_MS)
            return;

        /* 参考代码做法: 初始化期间关闭卡检测中断避免干扰 */
        SDH1->INTEN &= ~SDH_INTEN_CDIEN_Msk;
        ret = SDH_Probe(SDH1);
        SDH1->INTEN |= SDH_INTEN_CDIEN_Msk;

        if (ret == SDH_NO_SD_CARD) {
            SD_DEBUG("init failed: no card");
            g_sd_state = SD_STATE_NO_CARD;
            g_sd_info.inserted = 0;
            return;
        }
        if (ret != 0) {
            SD_DEBUG("init failed: err=0x%x, retry on next card event", (unsigned int)ret);
            g_sd_state = SD_STATE_ERROR;
            return;
        }

        g_sd_info.card_type     = SD1.CardType;
        g_sd_info.total_sectors = SD1.totalSectorN;
        g_sd_info.disk_size_kb  = SD1.diskSize;
        g_sd_info.sector_size   = (uint16_t)SD1.sectorSize;
        g_sd_info.inserted      = 1;

        SD_DEBUG("init ok, type=%d, size=%dMB, mounting FS...",
                 (int)g_sd_info.card_type, (int)(g_sd_info.disk_size_kb / 1024));

        g_sd_state = SD_STATE_FS_MOUNT;
        break;

    case SD_STATE_FS_MOUNT:
        /* 卡拔出则退回 NO_CARD */
        if (changed && !SDH_IS_CARD_PRESENT(SDH1)) {
            SD_DEBUG("card removed during FS mount");
            g_sd_info.inserted = 0;
            g_sd_state = SD_STATE_NO_CARD;
            break;
        }

        if (g_sd_msc_owned) {
            g_sd_state = SD_STATE_READY;
            break;
        }

        ret = f_mount(&g_fatfs, "0:", 1);
        if (ret != FR_OK) {
            SD_DEBUG("f_mount err: %d", (int)ret);
            g_sd_state = SD_STATE_ERROR;
            return;
        }
        HW_DEBUG("sd card mount ok");
        g_sd_state = SD_STATE_READY;
        break;

    case SD_STATE_READY:
        if (changed && !SDH_IS_CARD_PRESENT(SDH1)) {
            SD_DEBUG("card removed");
            g_sd_info.inserted = 0;
            g_sd_state = SD_STATE_NO_CARD;
        }
        break;

    case SD_STATE_ERROR:
        if (changed) {
            SD_DEBUG("card event, retry init");
            g_sd_state = SD_STATE_NO_CARD;
        }
        break;
    }
}

/* ── 块读取 ── */
uint32_t SDCard_ReadBlocks(uint8_t *buf, uint32_t start_sector, uint32_t count)
{
    if (g_sd_state != SD_STATE_FS_MOUNT && g_sd_state != SD_STATE_READY)
        return SDCARD_ERR_NO_CARD;
    if (count == 0) return SDCARD_ERR_PARAM;

    if (SDH_Read(SDH1, buf, start_sector, count) != 0) {
        SD_DEBUG("read err: sec=%d, cnt=%d", (int)start_sector, (int)count);
        return SDCARD_ERR_READ;
    }
    return SDCARD_OK;
}

uint32_t SDCard_WriteBlocks(const uint8_t *buf, uint32_t start_sector, uint32_t count)
{
    if (g_sd_state != SD_STATE_FS_MOUNT && g_sd_state != SD_STATE_READY)
        return SDCARD_ERR_NO_CARD;
    if (count == 0) return SDCARD_ERR_PARAM;

    if (SDH_Write(SDH1, (uint8_t *)buf, start_sector, count) != 0) {
        SD_DEBUG("write err: sec=%d, cnt=%d", (int)start_sector, (int)count);
        return SDCARD_ERR_WRITE;
    }
    return SDCARD_OK;
}

static uint32_t SDCard_BeginTransfer(uint8_t op, uint8_t *buf, uint32_t start_sector, uint32_t count)
{
    uint32_t reg;
    if (g_sd_async_op)
        return SDCARD_BUSY;
    if (!SDCard_IsReady())
        return SDCARD_ERR_NO_CARD;
    if (count == 0 || count > 255)
        return SDCARD_ERR_PARAM;
    if (SDH_SDCmdAndRsp(SDH1, 7, SD1.RCA, 0) != Successful)
        return (op == SD_ASYNC_READ) ? SDCARD_ERR_READ : SDCARD_ERR_WRITE;

    SDH_CheckRB(SDH1);
    SDH1->BLEN = 512 - 1;
    SDH1->CMDARG = (SD1.CardType == SDH_TYPE_SD_HIGH || SD1.CardType == SDH_TYPE_EMMC)
                 ? start_sector : start_sector * 512;
    SDH1->DMASA = (uint32_t)buf;
    g_u8SDDataReadyFlag = 0;
    g_sd_async_error = 0;

    if (op == SD_ASYNC_READ) {
        reg = SDH1->CTL & ~(SDH_CTL_CMDCODE_Msk | SDH_CTL_BLKCNT_Msk);
        SDH1->CTL = reg | (count << SDH_CTL_BLKCNT_Pos) | (18 << SDH_CTL_CMDCODE_Pos) |
                    SDH_CTL_COEN_Msk | SDH_CTL_RIEN_Msk | SDH_CTL_DIEN_Msk;
    } else {
        reg = SDH1->CTL & 0xFF00C080;
        SDH1->CTL = reg | (count << SDH_CTL_BLKCNT_Pos) | (25 << SDH_CTL_CMDCODE_Pos) |
                    SDH_CTL_COEN_Msk | SDH_CTL_RIEN_Msk | SDH_CTL_DOEN_Msk;
    }
    g_sd_async_op = op;
    return SDCARD_OK;
}

uint32_t SDCard_BeginReadBlocks(uint8_t *buf, uint32_t start_sector, uint32_t count)
{
    return SDCard_BeginTransfer(SD_ASYNC_READ, buf, start_sector, count);
}

uint32_t SDCard_BeginWriteBlocks(const uint8_t *buf, uint32_t start_sector, uint32_t count)
{
    return SDCard_BeginTransfer(SD_ASYNC_WRITE, (uint8_t *)buf, start_sector, count);
}

uint32_t SDCard_PollTransfer(void)
{
    uint8_t op = g_sd_async_op;

    if (!op)
        return SDCARD_OK;
    if (!SDH_IS_CARD_PRESENT(SDH1)) {
        SDCard_AbortTransfer();
        return SDCARD_ERR_NO_CARD;
    }
    if (!g_u8SDDataReadyFlag)
        return SDCARD_BUSY;

    if (g_sd_async_error) {
        SDCard_AbortTransfer();
        return (op == SD_ASYNC_READ) ? SDCARD_ERR_READ : SDCARD_ERR_WRITE;
    }
    if (op == SD_ASYNC_READ) {
        if ((SDH1->INTSTS & SDH_INTSTS_CRC7_Msk) != SDH_INTSTS_CRC7_Msk ||
            (SDH1->INTSTS & SDH_INTSTS_CRC16_Msk) != SDH_INTSTS_CRC16_Msk) {
            SDCard_AbortTransfer();
            return SDCARD_ERR_READ;
        }
    } else if (SDH1->INTSTS & SDH_INTSTS_CRCIF_Msk) {
        SDH1->INTSTS = SDH_INTSTS_CRCIF_Msk;
        SDCard_AbortTransfer();
        return SDCARD_ERR_WRITE;
    }
    if (SDH_SDCmdAndRsp(SDH1, 12, 0, 0) != Successful) {
        SDCard_AbortTransfer();
        return (op == SD_ASYNC_READ) ? SDCARD_ERR_READ : SDCARD_ERR_WRITE;
    }
    SDH_CheckRB(SDH1);
    SDH_SDCommand(SDH1, 7, 0);
    SDH1->CTL |= SDH_CTL_CLK8OEN_Msk;
    while (SDH1->CTL & SDH_CTL_CLK8OEN_Msk) {
    }
    g_sd_async_op = 0;
    return SDCARD_OK;
}

void SDCard_AbortTransfer(void)
{
    uint8_t active = g_sd_async_op;

    g_sd_async_op = 0;
    g_sd_async_error = 0;
    SDH1->CTL |= SDH_CTL_CTLRST_Msk;
    while (SDH1->CTL & SDH_CTL_CTLRST_Msk) {
    }
    if (active && SDH_IS_CARD_PRESENT(SDH1)) {
        SDH_SDCmdAndRsp(SDH1, 12, 0, 0);
        SDH_CheckRB(SDH1);
        SDH_SDCommand(SDH1, 7, 0);
        SDH1->CTL |= SDH_CTL_CLK8OEN_Msk;
        while (SDH1->CTL & SDH_CTL_CLK8OEN_Msk) {
        }
    }
}

const sdcard_info_t* SDCard_GetInfo(void)
{
    return &g_sd_info;
}

uint8_t SDCard_IsInserted(void)
{
    return SDH_IS_CARD_PRESENT(SDH1);
}

uint8_t SDCard_IsReady(void)
{
    /* 卡物理层已就绪 (SDH_Probe 通过)，可做块读写 */
    return (g_sd_state == SD_STATE_FS_MOUNT || g_sd_state == SD_STATE_READY) ? 1 : 0;
}

uint8_t SDCard_IsMounted(void)
{
    /* 文件系统已挂载 (f_mount 通过)，可做文件操作 */
    return (g_sd_state == SD_STATE_READY && !g_sd_msc_owned) ? 1 : 0;
}

void SDCard_AcquireForMSC(void)
{
    if (g_sd_msc_owned) return;

    if (g_sd_state == SD_STATE_READY)
        f_mount(NULL, "0:", 0);

    g_sd_msc_owned = 1;
}

void SDCard_ReleaseFromMSC(void)
{
    if (!g_sd_msc_owned) return;

    g_sd_msc_owned = 0;
    if (g_sd_state == SD_STATE_READY)
        g_sd_state = SD_STATE_FS_MOUNT;
}

uint8_t SDCard_IsOwnedByMSC(void)
{
    return g_sd_msc_owned;
}

/* ── SDH1 中断处理 (完全按参考代码 SDH0_IRQHandler 逻辑) ── */
__attribute__((used))
void SDH1_IRQHandler(void)
{
    uint32_t volatile isr;

    isr = SDH1->INTSTS;

    /* 块数据传输完成 */
    if (isr & SDH_INTSTS_BLKDIF_Msk) {
        g_u8SDDataReadyFlag = 1;
        SDH1->INTSTS = SDH_INTSTS_BLKDIF_Msk;
    }

    /* 卡检测 — 参考代码: 延时后重新读取 INTSTS 以确保 CDSTS 稳定 */
    if (isr & SDH_INTSTS_CDIF_Msk) {
        int volatile i;
        for (i = 0; i < 0x500; i++);   /* delay 50 times, SD_CLK = 200KHz OK */
        isr = SDH1->INTSTS;            /* 重新读取 */

        if (isr & SDH_INTSTS_CDSTS_Msk) {
            /* CDSTS=1 表示卡拔出 (GPIO 模式) */
            SD1.IsCardInsert = 0;
            memset(&SD1, 0, sizeof(SDH_INFO_T));
            g_sd_card_changed = 1;
        } else {
            /* CDSTS=0 表示卡插入 */
            g_sd_card_changed = 1;
        }

        SDH1->INTSTS = SDH_INTSTS_CDIF_Msk;
    }

    /* CRC 错误 */
    if (isr & SDH_INTSTS_CRCIF_Msk) {
        if (!(isr & SDH_INTSTS_CRC16_Msk)) {
            /* CRC16 error */
            g_sd_async_error = 1;
        } else if (!(isr & SDH_INTSTS_CRC7_Msk)) {
            if (!g_u8R3Flag) {
                /* CRC7 error (R3 response has no CRC, g_u8R3Flag skips it) */
                g_sd_async_error = 1;
            }
        }
        SDH1->INTSTS = SDH_INTSTS_CRCIF_Msk;
    }

    /* 数据输入超时 */
    if (isr & SDH_INTSTS_DITOIF_Msk) {
        g_sd_async_error = 1;
        SDH1->INTSTS |= SDH_INTSTS_DITOIF_Msk;
    }

    /* 响应超时 */
    if (isr & SDH_INTSTS_RTOIF_Msk) {
        g_sd_async_error = 1;
        SDH1->INTSTS |= SDH_INTSTS_RTOIF_Msk;
    }
}
