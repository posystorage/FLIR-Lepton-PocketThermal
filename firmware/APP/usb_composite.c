#include <string.h>
#include "usb_composite.h"
#include "usb_uvc.h"
#include "sdcard.h"
#include "debug.h"
#include "sys_tick.h"

#define USB_BUS_INT_COMMON (HSUSBD_BUSINTEN_RSTIEN_Msk | \
                            HSUSBD_BUSINTEN_VBUSDETIEN_Msk | \
                            HSUSBD_BUSINTEN_HISPDIEN_Msk)
#define MSC_WAIT_TIMEOUT_MS  2000
#define MSC_REENUM_GRACE_MS  3000
#define MSC_CSW_PASSED       0
#define MSC_CSW_FAILED       1
#define MSC_BULK_NORMAL      0xFF
#define MSC_BULK_CBW         0x00
#define MSC_BULK_OUT         0x02
#define MSC_BULK_CSW_PENDING 0x03
#define MSC_BULK_ZLP_PENDING 0x04
#define MSC_PROTOCOL_BUF_SIZE 0x100
#define MSC_DATA_BUF_SIZE     0x1000
#define MSC_IO_NONE           0
#define MSC_IO_READ           1
#define MSC_IO_WRITE          2
#define MSC_BUF_FREE          0
#define MSC_BUF_SD_BUSY       1
#define MSC_BUF_USB_BUSY      2
#define MSC_BUF_USB_READY     3
#define MSC_BUF_SD_READY      4
#define MSC_BUF_INVALID       0xFF

/* ===== Descriptors ===== */
/* Device descriptor — MI device (class defined via IAD) */
const uint8_t gu8DeviceDescriptor[] __attribute__((aligned(4))) = {
    LEN_DEVICE,    DESC_DEVICE,
    0x00, 0x02,                     /* bcdUSB 2.00 */
    0xEF,                           /* bDeviceClass = MI device */
    0x02,                           /* bDeviceSubClass */
    0x01,                           /* bDeviceProtocol */
    CEP_MAX_PKT_SIZE,               /* bMaxPacketSize0 */
    (USBD_VID & 0xFF),
    ((USBD_VID >> 8) & 0xFF),
    (USBD_PID & 0xFF),
    ((USBD_PID >> 8) & 0xFF),
    0x00, 0x00,                     /* bcdDevice */
    0x01,                           /* iManufacturer */
    0x02,                           /* iProduct */
    0x03,                           /* iSerialNumber */
    0x01                            /* bNumConfigurations */
};

/* Qualifier descriptor */
const uint8_t gu8QualifierDescriptor[] __attribute__((aligned(4))) = {
    LEN_QUALIFIER, DESC_QUALIFIER,
    0x00, 0x02,                     /* bcdUSB 2.00 */
    0xEF, 0x02, 0x01,
    CEP_OTHER_MAX_PKT_SIZE,
    0x01,                           /* bNumConfigurations */
    0x00
};
/* HS config descriptor — UVC + MSC composite (202 bytes) */
/* IAD(Video), IF0(VC), IF1(VS), IF2(MSC) */
const uint8_t gu8ConfigDescriptor[] __attribute__((aligned(4))) = {
    /* Config header: 9 bytes, tot=202, 3 IF, 100mA */
    0x09, 0x02, 0xCA, 0x00, 0x03, 0x01, 0x00, 0x80,0x32,
    
    /* IAD: Video function, interfaces 0-1 */
    0x08, 0x0B, 0x00, 0x02, 0x0E, 0x03, 0x00, 0x00,
    /* IF0: VC, 0 endpoints */
    0x09, 0x04, 0x00, 0x00, 0x00, 0x0E, 0x01, 0x00,0x00,
    
    /* VC Header (13 bytes): UVC1.10, wTotalLength=52, dwClockFreq=48MHz */
    0x0D, 0x24, 0x01, 0x10, 0x01, 0x34, 0x00, 0x00,
    0x6C, 0xDC, 0x02, 0x01, 0x01,
    /* Camera Terminal (18 bytes): ITT_CAMERA, ID=1 */
    0x12, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x00,
    /* Processing Unit (12 bytes): ID=2, source=Camera */
    0x0C, 0x24, 0x05, 0x02, 0x01, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00,
    /* Output Terminal (9 bytes): TT_STREAM, ID=3, src=PU */
    0x09, 0x24, 0x03, 0x03, 0x01, 0x01, 0x00, 0x02,
    0x00,
    /* IF1 VS alt0, 0 endpoints */
    0x09, 0x04, 0x01, 0x00, 0x00, 0x0E, 0x02, 0x00,0x00,    
    /* VS Input Header (13 bytes): 1 format, wTL=76, EP1 IN */
    0x0D, 0x24, 0x01, 0x01, 0x4C, 0x00, 0x81, 0x00,
    0x03, 0x00, 0x00, 0x01, 0x00,
    /* Format: Uncompressed (27 bytes) */
    0x1B, 0x24, 0x04, 0x01, 0x01,
    /* GUID: YUY2 {32595559-0000-0010-8000-00AA00389B71} */
    0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
    /* 16bpp, frame=1, aspect=0, interlace=0, copy=0 */
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
    /* Frame: 320x240, 8fps (30 bytes) */
    0x1E, 0x24, 0x05, 0x01, 0x00,
    0x40, 0x01,                     /* wWidth = 320 */
    0xF0, 0x00,                     /* wHeight = 240 */
    /* dwMinBitRate / dwMaxBitRate = 320*240*16*8 = 9830400 */
    0x00, 0x00, 0x96, 0x00,
    0x00, 0x00, 0x96, 0x00,
    /* dwMaxVideoFrameBufferSize = 320*240*2 = 153600 */
    0x00, 0x58, 0x02, 0x00,
    /* dwDefaultFrameInterval = 1250000 (100ns) = 8fps */
    0xD0, 0x12, 0x13, 0x00,
    /* 1 interval */
    0x01,
    0xD0, 0x12, 0x13, 0x00,
    /* Color Matching (6 bytes): BT.709, SMPTE170M */
    0x06, 0x24, 0x0D, 0x01, 0x01, 0x04,
    /* IF1 VS alt1, 1 endpoint */
    0x09, 0x04, 0x01, 0x01, 0x01, 0x0E, 0x02, 0x00,0x00,
    /* Isochronous EP1 IN: 1024B, 1 trans/μframe */
    0x07, 0x05, (1 | 0x80), 0x01, 0x00, 0x04, 0x01,
		
    /* IF2: MSC, 2 endpoints */
    0x09, 0x04, 0x02, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    
    /* Bulk EP2 IN, 512B */
    0x07, 0x05, (2 | 0x80), 0x02, 0x00, 0x02, 0x00,
    /* Bulk EP2 OUT, 512B */
    0x07, 0x05, (2 | 0x00), 0x02, 0x00, 0x02, 0x00
};
/* FS config descriptor (MSC only) */
static const uint8_t gu8ConfigDescFS[] __attribute__((aligned(4))) = {
    /* Config: tot=32, 1 IF, MSC, 100mA */
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    
    /* IF0: MSC, 2 endpoints */
    0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    
    /* Bulk IN EP2, 64B */
    0x07, 0x05, (2 | 0x80), 0x02, 0x40, 0x00, 0x00,
    /* Bulk OUT EP2, 64B */
    0x07, 0x05, (2 | 0x00), 0x02, 0x40, 0x00, 0x00
};

/* HS other-speed descriptor — FS view of the same MSC config */
static const uint8_t gu8OtherDescHS[] __attribute__((aligned(4))) = {
    0x09, 0x07, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    
    0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    
    0x07, 0x05, (2 | 0x80), 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, (2 | 0x00), 0x02, 0x40, 0x00, 0x00
};
/* FS other-speed descriptor — HS config with DESC_OTHERSPEED tag */
static const uint8_t gu8OtherDescFS[] __attribute__((aligned(4))) = {
    /* Config: tot=202, 3 IF, 100mA */
    0x09, 0x07, 0xCA, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,
    
    /* IAD: Video, IF0-1 */
    0x08, 0x0B, 0x00, 0x02, 0x0E, 0x03, 0x00, 0x00,
    /* IF0: VC, 0 EP */
    0x09, 0x04, 0x00, 0x00, 0x00, 0x0E, 0x01, 0x00, 0x00,
    
    /* VC Header (13 bytes) */
    0x0D, 0x24, 0x01, 0x10, 0x01, 0x34, 0x00, 0x00,
    0x6C, 0xDC, 0x02, 0x01, 0x01,
    /* Camera Terminal (18 bytes) */
    0x12, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x00,
    /* Processing Unit (12 bytes) */
    0x0C, 0x24, 0x05, 0x02, 0x01, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00,
    /* Output Terminal (9 bytes) */
    0x09, 0x24, 0x03, 0x03, 0x01, 0x01, 0x00, 0x02, 0x00,
    
    /* IF1 VS alt0 */
    0x09, 0x04, 0x01, 0x00, 0x00, 0x0E, 0x02, 0x00, 0x00,
    
    /* VS Input Header (13 bytes) */
    0x0D, 0x24, 0x01, 0x01, 0x4C, 0x00, 0x81, 0x00,
    0x03, 0x00, 0x00, 0x01, 0x00,
    /* Format: Uncompressed (27 bytes) */
    0x1B, 0x24, 0x04, 0x01, 0x01,
    0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
    /* Frame: 320x240, 8fps (30 bytes) */
    0x1E, 0x24, 0x05, 0x01, 0x00,
    0x40, 0x01,                     /* wWidth = 320 */
    0xF0, 0x00,                     /* wHeight = 240 */
    0x00, 0x00, 0x96, 0x00,        /* dwMinBitRate */
    0x00, 0x00, 0x96, 0x00,        /* dwMaxBitRate */
    0x00, 0x58, 0x02, 0x00,        /* dwMaxVideoFrameSize */
    0xD0, 0x12, 0x13, 0x00,        /* dwDefaultFrameInterval = 8fps */
    0x01,                           /* 1 interval */
    0xD0, 0x12, 0x13, 0x00,
    /* Color Matching (6 bytes) */
    0x06, 0x24, 0x0D, 0x01, 0x01, 0x04,
    /* IF1 VS alt1 */
    0x09, 0x04, 0x01, 0x01, 0x01, 0x0E, 0x02, 0x00, 0x00,
    /* Isoch EP1 IN: 512B, 1 trans/μframe */
    0x07, 0x05, (1 | 0x80), 0x01, 0x00, 0x02, 0x01,
    /* IF2: MSC */
    0x09, 0x04, 0x02, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    
    /* Bulk EP2 IN */
    0x07, 0x05, (2 | 0x80), 0x02, 0x00, 0x02, 0x00,
    /* Bulk EP2 OUT */
    0x07, 0x05, (2 | 0x00), 0x02, 0x00, 0x02, 0x00
};
/* Strings */
static const uint8_t gu8StringLang[] __attribute__((aligned(4))) = {
    4, DESC_STRING, 0x09, 0x04
};
static const uint8_t gu8VendorStr[] __attribute__((aligned(4))) = {
    24, DESC_STRING,
    'P',0, 'O',0, 'S',0, 'Y',0, 'S',0, 'T',0, 'O',0, 'R',0,
    'A',0, 'G',0, 'E',0
};
static const uint8_t gu8ProductStr[] __attribute__((aligned(4))) = {
    54, DESC_STRING,
    'F',0, 'L',0, 'I',0, 'R',0, ' ',0, 'L',0, 'e',0, 'p',0,
    't',0, 'o',0, 'n',0, ' ',0, 'T',0, 'h',0, 'e',0, 'r',0,
    'm',0, 'a',0, 'l',0, ' ',0, 'C',0, 'a',0, 'm',0, 'e',0,
    'r',0, 'a',0
};
static const uint8_t gu8SerialStr[] __attribute__((aligned(4))) = {
    12, DESC_STRING,
    '0',0, '0',0, '0',0, '0',0, '0',0
};

static uint8_t *gpu8StrTbl[4] = {
    (uint8_t *)gu8StringLang, (uint8_t *)gu8VendorStr,
    (uint8_t *)gu8ProductStr, (uint8_t *)gu8SerialStr
};

S_HSUSBD_INFO_T gsHSInfo = {
    (uint8_t*)gu8DeviceDescriptor, (uint8_t*)gu8ConfigDescriptor, gpu8StrTbl,
    (uint8_t*)gu8QualifierDescriptor, (uint8_t*)gu8ConfigDescFS,
    (uint8_t*)gu8OtherDescHS, (uint8_t*)gu8OtherDescFS,
    NULL, NULL, NULL, NULL
};
/* ===== State ===== */
static volatile uint8_t g_u8MscStart = 0, g_u8BulkState = MSC_BULK_NORMAL;
static uint8_t g_u8Prevent = 0, g_au8SenseKey[4], g_u8MscConfigured = 0;
static volatile uint8_t g_uvc_configured = 0, g_uvc_streaming = 0;

static uint32_t g_u32MassBase = 0;
static uint8_t g_msc_protocol_buf[MSC_PROTOCOL_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t g_msc_data_buf0[MSC_DATA_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t g_msc_data_buf1[MSC_DATA_BUF_SIZE] __attribute__((aligned(4)));
typedef struct {
    uint8_t *data;
    uint8_t state;
    uint32_t lba;
    uint32_t sectors;
    uint32_t start_tick;
} MSC_IO_BUF_T;
typedef struct {
    uint8_t op;
    uint8_t sd_busy;
    uint8_t usb_busy;
    uint8_t sd_next;
    uint8_t usb_next;
    uint32_t next_lba;
    uint32_t sectors_left;
    uint32_t completed_bytes;
    MSC_IO_BUF_T buf[2];
} MSC_IO_T;
static MSC_IO_T g_msc_io;

static uint32_t g_u32EpMaxPacketSize;
static volatile int32_t g_TotalSectors = 0;
static volatile uint8_t g_u8MscMediaReady = 0, g_u8MscUnitAttention = 0, g_u8MscReleasePending = 0;
static volatile uint8_t g_u8MscClassResetEvent = 0;
static volatile uint8_t g_u8MscAbortPending = 0;
static uint8_t g_u8MscStallEp = 0;
static volatile uint8_t g_u8MscReenumPending = 0;
static uint32_t g_u32MscReenumTick = 0;
/* bus event flags for debug (set in ISR, printed in Service) */
static volatile uint8_t g_u8BusReset = 0, g_u8BusResume = 0, g_u8BusSuspend = 0, g_u8BusVbus = 0, g_u8BusSpeed = 0, g_u8IsHS = 0;
static volatile uint8_t g_u8UsbStarted = 0;
static volatile uint8_t g_u8UsbConnectEvent = 0, g_u8UsbDetachEvent = 0;
/* CEP debug: use counters to track multi-packet transfers */
static volatile uint8_t g_u8CepSetup = 0, g_u8CepSetupReq = 0;
static volatile uint8_t g_u8CepIntkCnt = 0, g_u8CepCtrlInCnt = 0, g_u8CepTxDoneCnt = 0, g_u8CepStsDone = 0;
typedef struct {
    uint32_t sig, tag, len;
    uint8_t flags, lun, cblen;
    uint8_t cdb[16];
}
CBW;
typedef struct {
    uint32_t sig, tag, resid;
    uint8_t status;
}
CSW;
static CBW g_cbw;
static CSW g_csw;
static __INLINE uint32_t get_be32(const uint8_t * b) {
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}
/* ===== EP Init ===== */
static void EpInitHS(void) {
    HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPA, 1, HSUSBD_EP_CFG_TYPE_ISO, HSUSBD_EP_CFG_DIR_IN);
    HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPB, 2, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_IN);
    HSUSBD_SetEpBufAddr(EPC, EPC_BUF_BASE, EPC_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPC, EPC_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPC, 2, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_OUT);
    HSUSBD_ENABLE_EP_INT(EPC, HSUSBD_EPINTEN_RXPKIEN_Msk);
    g_u32EpMaxPacketSize = EPB_MAX_PKT_SIZE;
}
static void EpInitFS(void) {
    HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_OTHER_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPB, 2, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_IN);
    HSUSBD_SetEpBufAddr(EPC, EPC_BUF_BASE, EPC_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPC, EPC_OTHER_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPC, 2, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_OUT);
    HSUSBD_ENABLE_EP_INT(EPC, HSUSBD_EPINTEN_RXPKIEN_Msk);
    g_u32EpMaxPacketSize = EPB_OTHER_MAX_PKT_SIZE;
}
/* ===== DMA ===== */
static uint8_t MSC_WaitEpBufEmpty(uint32_t ep) {
    uint32_t start = GetTick();

    while (!(HSUSBD_GET_EP_INT_FLAG(ep) & HSUSBD_EPINTSTS_BUFEMPTYIF_Msk)) {
        if (!g_u8MscStart || !HSUSBD_IS_ATTACHED())
            return 0;
        if ((int32_t)(GetTick() - start) >= MSC_WAIT_TIMEOUT_MS)
            return 0;
    }
    return 1;
}
static uint8_t MSC_ActiveDMA(uint32_t a, uint32_t l) {
    uint32_t start = GetTick();

    HSUSBD_ENABLE_BUS_INT(USB_BUS_INT_COMMON | HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
    HSUSBD_SET_DMA_ADDR(a);
    HSUSBD_SET_DMA_LEN(l);
    g_hsusbd_DmaDone = 0;
    HSUSBD_ENABLE_DMA();
    while (g_u8MscStart) {
        if (g_hsusbd_DmaDone) return 1;
        if (!HSUSBD_IS_ATTACHED()) break;
        if ((int32_t)(GetTick() - start) >= MSC_WAIT_TIMEOUT_MS) break;
    }
    HSUSBD_ResetDMA();
    return 0;
}
static uint8_t MSC_BulkIn(uint32_t a, uint32_t l) {
    uint32_t i = 0, r = 0, c = 0;
    HSUSBD_SET_DMA_READ(2);
    for (i = 0; i < l / USBD_MAX_DMA_LEN; i++) {
        HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_TXPKIEN_Msk);
        g_hsusbd_ShortPacket = 0;
        if (!MSC_WaitEpBufEmpty(EPB)) return 0;
        if (!MSC_ActiveDMA(a + i * USBD_MAX_DMA_LEN, USBD_MAX_DMA_LEN)) return 0;
    }
    a += i * USBD_MAX_DMA_LEN;
    r = l % USBD_MAX_DMA_LEN;
    if (r) {
        c = r / g_u32EpMaxPacketSize;
        if (c) {
            HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_TXPKIEN_Msk);
            g_hsusbd_ShortPacket = 0;
            if (!MSC_WaitEpBufEmpty(EPB)) return 0;
            if (!MSC_ActiveDMA(a, c * g_u32EpMaxPacketSize)) return 0;
            a += c * g_u32EpMaxPacketSize;
        }
        c = r % g_u32EpMaxPacketSize;
        if (c) {
            HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_TXPKIEN_Msk);
            g_hsusbd_ShortPacket = 1;
            if (!MSC_WaitEpBufEmpty(EPB)) return 0;
            if (!MSC_ActiveDMA(a, c)) return 0;
        }
    }
    return 1;
}
static uint8_t MSC_BulkOut(uint32_t a, uint32_t l) {
    uint32_t i = 0, r = 0;
    HSUSBD_SET_DMA_WRITE(2);
    g_hsusbd_ShortPacket = 0;
    for (i = 0; i < l / USBD_MAX_DMA_LEN; i++)
        if (!MSC_ActiveDMA(a + i * USBD_MAX_DMA_LEN, USBD_MAX_DMA_LEN)) return 0;
    r = l % USBD_MAX_DMA_LEN;
    if (r && !MSC_ActiveDMA(a + i * USBD_MAX_DMA_LEN, r)) return 0;
    return 1;
}
static void MSC_ReceiveCBW(uint32_t b) {
    HSUSBD_SET_DMA_WRITE(2);
    HSUSBD_ENABLE_BUS_INT(USB_BUS_INT_COMMON | HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
    HSUSBD_SET_DMA_ADDR(b);
    HSUSBD_SET_DMA_LEN(31);
    g_hsusbd_DmaDone = 0;
    HSUSBD_ENABLE_DMA();
}
static uint8_t MSC_TryStartDMA(uint8_t dir_in, uint32_t addr, uint32_t len) {
    if (HSUSBD->DMACTL & HSUSBD_DMACTL_DMAEN_Msk)
        return 0;
    if (dir_in && !(HSUSBD_GET_EP_INT_FLAG(EPB) & HSUSBD_EPINTSTS_BUFEMPTYIF_Msk))
        return 0;

    if (dir_in) {
        HSUSBD_SET_DMA_READ(2);
        HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_TXPKIEN_Msk);
    } else {
        HSUSBD_SET_DMA_WRITE(2);
    }
    g_hsusbd_ShortPacket = 0;
    HSUSBD_ENABLE_BUS_INT(USB_BUS_INT_COMMON | HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
    HSUSBD_SET_DMA_ADDR(addr);
    HSUSBD_SET_DMA_LEN(len);
    g_hsusbd_DmaDone = 0;
    HSUSBD_ENABLE_DMA();
    return 1;
}
static uint8_t MSC_BulkInProtocol(uint32_t l) {
    if (l > MSC_PROTOCOL_BUF_SIZE)
        l = MSC_PROTOCOL_BUF_SIZE;
    return MSC_BulkIn(g_u32MassBase, l);
}
static void MSC_SendCSW(void) {
    MSC_DEBUG("CSW tag=%08lX residue=%lu status=%u",
              (unsigned long)g_csw.tag, (unsigned long)g_csw.resid,
              (unsigned int)g_csw.status);
    memcpy((void * ) g_u32MassBase, & g_csw.sig, 16);
    MSC_BulkInProtocol(13);
    g_u8BulkState = MSC_BULK_NORMAL;
}
static void MSC_AckCmd(uint32_t r, uint8_t status) {
    g_csw.resid = r;
    g_csw.status = status;
    MSC_SendCSW();
}
static void MSC_FailDataPhase(uint32_t r) {
    g_csw.resid = r;
    g_csw.status = MSC_CSW_FAILED;
    if (g_cbw.len == 0) {
        MSC_SendCSW();
        return;
    }

    if (g_cbw.flags & 0x80) {
        HSUSBD->EP[EPB].EPRSPCTL =
            (HSUSBD->EP[EPB].EPRSPCTL & HSUSBD_EP_RSPCTL_HALT) |
            HSUSBD_EP_RSPCTL_ZEROLEN;
        g_u8BulkState = MSC_BULK_ZLP_PENDING;
        MSC_DEBUG("ZLP op=%02X dir=IN residue=%lu sense=%02X/%02X/%02X",
                  (unsigned int)g_cbw.cdb[0], (unsigned long)r,
                  (unsigned int)g_au8SenseKey[0], (unsigned int)g_au8SenseKey[1],
                  (unsigned int)g_au8SenseKey[2]);
        return;
    }

    g_u8MscStallEp = (g_cbw.flags & 0x80) ? EPB : EPC;
    HSUSBD_SetEpStall(g_u8MscStallEp);
    g_u8BulkState = MSC_BULK_CSW_PENDING;
    MSC_DEBUG("STALL ep=%u op=%02X dir=%s residue=%lu sense=%02X/%02X/%02X",
              (unsigned int)g_u8MscStallEp, (unsigned int)g_cbw.cdb[0],
              (g_cbw.flags & 0x80) ? "IN" : "OUT", (unsigned long)r,
              (unsigned int)g_au8SenseKey[0], (unsigned int)g_au8SenseKey[1],
              (unsigned int)g_au8SenseKey[2]);
}
/* ===== SCSI ===== */
static
const uint8_t g_inqID[36] __attribute__((aligned(4))) = {
    0,
    0x80,
    0,
    0,
    0x1F,
    0,
    0,
    0,
    'P',
    'O',
    'S',
    'Y',
    'S',
    'T',
    'O',
    'R',
    'F',
    'L',
    'I',
    'R',
    ' ',
    'L',
    'e',
    'p',
    't',
    'o',
    'n',
    ' ',
    'T',
    'h',
    'e',
    'r',
    '1',
    '.',
    '0',
    '0'
};
static void SCSI_ReqSense(void) {
    memset((void * ) g_u32MassBase, 0, 18);*(uint8_t * ) g_u32MassBase = g_u8Prevent ? 0x70 : 0xF0;*((uint8_t * ) g_u32MassBase + 2) = g_au8SenseKey[0];*((uint8_t * ) g_u32MassBase + 7) = 0x0A;*((uint8_t * ) g_u32MassBase + 12) = g_au8SenseKey[1];*((uint8_t * ) g_u32MassBase + 13) = g_au8SenseKey[2];
    MSC_BulkInProtocol(g_cbw.len);
    g_u8Prevent = 0;
    memset(g_au8SenseKey, 0, 3);
}
static void MSC_SetSense(uint8_t key, uint8_t asc, uint8_t ascq) {
    g_au8SenseKey[0] = key;
    g_au8SenseKey[1] = asc;
    g_au8SenseKey[2] = ascq;
}
static uint8_t MSC_RequireMedia(void) {
    if (g_u8MscUnitAttention) {
        g_u8MscUnitAttention = 0;
        MSC_SetSense(0x06, 0x28, 0x00);
        MSC_DEBUG("sense UNIT ATTENTION: medium may have changed");
        return 0;
    }
    if (!SDCard_IsReady()) {
        MSC_SetSense(0x02, 0x3A, 0x00);
        MSC_DEBUG("sense NOT READY: medium not present");
        return 0;
    }
    return 1;
}
static void MSC_ResetAsyncTransfer(void) {
    memset(&g_msc_io, 0, sizeof(g_msc_io));
    g_msc_io.buf[0].data = g_msc_data_buf0;
    g_msc_io.buf[1].data = g_msc_data_buf1;
    g_msc_io.sd_busy = MSC_BUF_INVALID;
    g_msc_io.usb_busy = MSC_BUF_INVALID;
}
static void MSC_AbortAsyncTransfer(void) {
    if (g_msc_io.op && g_msc_io.sd_busy != MSC_BUF_INVALID)
        SDCard_AbortTransfer();
    HSUSBD_ResetDMA();
    MSC_ResetAsyncTransfer();
}
static void MSC_FailAsyncTransfer(void) {
    uint8_t op = g_msc_io.op;
    uint32_t residue = g_cbw.len - g_msc_io.completed_bytes;

    MSC_DEBUG("async %c fail residue=%lu", (op == MSC_IO_READ) ? 'R' : 'W',
              (unsigned long)residue);
    MSC_AbortAsyncTransfer();
    MSC_SetSense(0x03, (op == MSC_IO_READ) ? 0x11 : 0x0C, 0x00);
    MSC_FailDataPhase(residue);
}
static void MSC_StartAsyncTransfer(uint8_t op, uint32_t lba, uint32_t sectors) {
    MSC_ResetAsyncTransfer();
    g_msc_io.op = op;
    g_msc_io.next_lba = lba;
    g_msc_io.sectors_left = sectors;
    MSC_DEBUG("async %c start L=%lu N=%lu", (op == MSC_IO_READ) ? 'R' : 'W',
              (unsigned long)lba, (unsigned long)sectors);
}
static void MSC_ServiceAsyncTransfer(void) {
    uint8_t idx;
    uint32_t result;
    uint32_t chunk;
    MSC_IO_BUF_T *buf;

    if (!g_msc_io.op)
        return;
    if (!HSUSBD_IS_ATTACHED() || !SDCard_IsReady()) {
        MSC_FailAsyncTransfer();
        return;
    }

    if (g_msc_io.usb_busy != MSC_BUF_INVALID) {
        idx = g_msc_io.usb_busy;
        buf = &g_msc_io.buf[idx];
        if (g_hsusbd_DmaDone) 
				{
						//PA15=1;//debug
						
            g_hsusbd_DmaDone = 0;
            g_msc_io.usb_busy = MSC_BUF_INVALID;
            if (g_msc_io.op == MSC_IO_READ) {
                g_msc_io.completed_bytes += buf->sectors * USBD_SECTOR_SIZE;
                buf->state = MSC_BUF_FREE;
            } else {
                buf->state = MSC_BUF_SD_READY;
            }
        } else if ((int32_t)(GetTick() - buf->start_tick) >= MSC_WAIT_TIMEOUT_MS) {
            MSC_FailAsyncTransfer();
            return;
        }
    }

    if (g_msc_io.sd_busy != MSC_BUF_INVALID) 
		{
        idx = g_msc_io.sd_busy;
        buf = &g_msc_io.buf[idx];
        result = SDCard_PollTransfer();
        if (result == SDCARD_OK) 
				{
						//PA14=1;//debug
						//__NOP();
						//__NOP();
						//__NOP();
            g_msc_io.sd_busy = MSC_BUF_INVALID;
            if (g_msc_io.op == MSC_IO_READ) 
						{
                buf->state = MSC_BUF_USB_READY;
            } 
						else 
					  {
                g_msc_io.completed_bytes += buf->sectors * USBD_SECTOR_SIZE;
                buf->state = MSC_BUF_FREE;
            }
        } else if (result != SDCARD_BUSY ||
                   (int32_t)(GetTick() - buf->start_tick) >= MSC_WAIT_TIMEOUT_MS) {
            MSC_FailAsyncTransfer();
            return;
        }
    }

    if (g_msc_io.op == MSC_IO_READ) 
		{
        idx = g_msc_io.usb_next;
        buf = &g_msc_io.buf[idx];
        if (g_msc_io.usb_busy == MSC_BUF_INVALID && buf->state == MSC_BUF_USB_READY &&
            MSC_TryStartDMA(1, (uint32_t)buf->data, buf->sectors * USBD_SECTOR_SIZE)) 
				{
					//PA15=0;//debug
            buf->state = MSC_BUF_USB_BUSY;
            buf->start_tick = GetTick();
            g_msc_io.usb_busy = idx;
            g_msc_io.usb_next ^= 1;
							
        }

        idx = g_msc_io.sd_next;
        buf = &g_msc_io.buf[idx];
        if (g_msc_io.sd_busy == MSC_BUF_INVALID && g_msc_io.sectors_left && buf->state == MSC_BUF_FREE)            
				{
            chunk = g_msc_io.sectors_left > (MSC_DATA_BUF_SIZE / USBD_SECTOR_SIZE)? (MSC_DATA_BUF_SIZE / USBD_SECTOR_SIZE) : g_msc_io.sectors_left;  
						//PA14=0;//debug
            result = SDCard_BeginReadBlocks(buf->data, g_msc_io.next_lba, chunk);
            if (result != SDCARD_OK) 
						{
                MSC_FailAsyncTransfer();
                return;
            }
            buf->lba = g_msc_io.next_lba;
            buf->sectors = chunk;
            buf->state = MSC_BUF_SD_BUSY;
            buf->start_tick = GetTick();
            g_msc_io.sd_busy = idx;
            g_msc_io.sd_next ^= 1;
            g_msc_io.next_lba += chunk;
            g_msc_io.sectors_left -= chunk;
						
        }
    } 
		else 
		{
        idx = g_msc_io.sd_next;
        buf = &g_msc_io.buf[idx];
        if (g_msc_io.sd_busy == MSC_BUF_INVALID && buf->state == MSC_BUF_SD_READY) 
				{
						//PA14=0;//debug
            result = SDCard_BeginWriteBlocks(buf->data, buf->lba, buf->sectors);
            if (result != SDCARD_OK) {
                MSC_FailAsyncTransfer();
                return;
            }
            buf->state = MSC_BUF_SD_BUSY;
            buf->start_tick = GetTick();
            g_msc_io.sd_busy = idx;
            g_msc_io.sd_next ^= 1;
        }

        idx = g_msc_io.usb_next;
        buf = &g_msc_io.buf[idx];
        if (g_msc_io.usb_busy == MSC_BUF_INVALID && g_msc_io.sectors_left &&
            buf->state == MSC_BUF_FREE) {
            chunk = g_msc_io.sectors_left > (MSC_DATA_BUF_SIZE / USBD_SECTOR_SIZE)
                  ? (MSC_DATA_BUF_SIZE / USBD_SECTOR_SIZE) : g_msc_io.sectors_left;
            if (MSC_TryStartDMA(0, (uint32_t)buf->data, chunk * USBD_SECTOR_SIZE)) 
						{
								//PA15=0;//debug
                buf->lba = g_msc_io.next_lba;
                buf->sectors = chunk;
                buf->state = MSC_BUF_USB_BUSY;
                buf->start_tick = GetTick();
                g_msc_io.usb_busy = idx;
                g_msc_io.usb_next ^= 1;
                g_msc_io.next_lba += chunk;
                g_msc_io.sectors_left -= chunk;
            }
        }
    }

    if (!g_msc_io.sectors_left && g_msc_io.sd_busy == MSC_BUF_INVALID &&
        g_msc_io.usb_busy == MSC_BUF_INVALID &&
        g_msc_io.buf[0].state == MSC_BUF_FREE && g_msc_io.buf[1].state == MSC_BUF_FREE) {
        MSC_DEBUG("async %c done bytes=%lu", (g_msc_io.op == MSC_IO_READ) ? 'R' : 'W',
                  (unsigned long)g_msc_io.completed_bytes);
        MSC_ResetAsyncTransfer();
        MSC_AckCmd(0, MSC_CSW_PASSED);
    }
}
static void MSC_UpdateMediaState(void) {
    uint8_t ready = SDCard_IsReady();

    if (ready == g_u8MscMediaReady) return;

    g_u8MscMediaReady = ready;
    g_u8MscUnitAttention = 1;
    if (ready) {
        const sdcard_info_t *info = SDCard_GetInfo();
        g_TotalSectors = info ? info->total_sectors : 0;
        MSC_DEBUG("MSC media ready, sectors=%lu", (unsigned long)g_TotalSectors);
    } else {
        g_TotalSectors = 0;
        MSC_DEBUG("MSC media removed");
    }
}
static void SCSI_ReadCap(void) {
    uint32_t t;
    uint8_t * p = (uint8_t * ) g_u32MassBase;
    if (g_TotalSectors <= 0) {
        memset(p, 0, 36);
        MSC_BulkInProtocol(g_cbw.len);
        return;
    }
    t = g_TotalSectors - 1;
    memset(p, 0, 36);
    p[0] = (uint8_t)(t >> 24);
    p[1] = (uint8_t)(t >> 16);
    p[2] = (uint8_t)(t >> 8);
    p[3] = (uint8_t) t;
    p[6] = 2;
    MSC_BulkInProtocol(g_cbw.len);
}
static void SCSI_ReadFmt(void) {
    uint8_t * p = (uint8_t * ) g_u32MassBase;
    memset(p, 0, 36);
    if (g_TotalSectors <= 0) {
        p[3] = 0x10;
        p[8] = 2;
        MSC_BulkInProtocol(g_cbw.len);
        return;
    }
    p[3] = 0x10;
    p[4] = (uint8_t)(g_TotalSectors >> 24);
    p[5] = (uint8_t)(g_TotalSectors >> 16);
    p[6] = (uint8_t)(g_TotalSectors >> 8);
    p[7] = (uint8_t) g_TotalSectors;
    p[8] = 2;
    p[10] = 2;
    p[12] = (uint8_t)(g_TotalSectors >> 24);
    p[13] = (uint8_t)(g_TotalSectors >> 16);
    p[14] = (uint8_t)(g_TotalSectors >> 8);
    p[15] = (uint8_t) g_TotalSectors;
    p[18] = 2;
    MSC_BulkInProtocol(g_cbw.len);
}
static void SCSI_Mode6(void) {
    uint8_t * p = (uint8_t * ) g_u32MassBase;
    memset(p, 0, 24);
    p[0] = 3;
    MSC_BulkInProtocol(g_cbw.len);
}
static const uint8_t g_modePage01[12] __attribute__((aligned(4))) = {
    0x01, 0x0A, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00
};
static const uint8_t g_modePage05[32] __attribute__((aligned(4))) = {
    0x05, 0x1E, 0x13, 0x88, 0x08, 0x20, 0x02, 0x00,
    0x01, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x05, 0x1E, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x68, 0x00, 0x00
};
static const uint8_t g_modePage1B[12] __attribute__((aligned(4))) = {
    0x1B, 0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
static const uint8_t g_modePage1C[8] __attribute__((aligned(4))) = {
    0x1C, 0x06, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00
};

static void SCSI_Mode10(void) {
    uint8_t * p = (uint8_t *)g_u32MassBase;
    uint8_t pg = g_cbw.cdb[2];
    uint8_t i, j;
    memset(p, 0, 72);
    p[0] = 67;
    p[3] = 0x08;
    switch (pg) {
    case 0x01:
        p[0] = 19;
        for (i = 8, j = 0; j < 12; i++, j++) p[i] = g_modePage01[j];
        break;
    case 0x05: {
        uint32_t cyl = g_TotalSectors / 128;
        p[0] = 39;
        for (i = 8, j = 0; j < 32; i++, j++) p[i] = g_modePage05[j];
        p[12] = 2; p[13] = 64;
        p[16] = (uint8_t)(cyl >> 8);
        p[17] = (uint8_t)cyl;
        break;
    }
    case 0x1B:
        p[0] = 19;
        for (i = 8, j = 0; j < 12; i++, j++) p[i] = g_modePage1B[j];
        break;
    case 0x1C:
        p[0] = 15;
        for (i = 8, j = 0; j < 8; i++, j++) p[i] = g_modePage1C[j];
        break;
    case 0x3F: {
        uint32_t cyl = g_TotalSectors / 128;
        p[0] = 71;
        i = 8;
        for (j = 0; j < 12; j++, i++) p[i] = g_modePage01[j];
        for (j = 0; j < 32; j++, i++) p[i] = g_modePage05[j];
        for (j = 0; j < 12; j++, i++) p[i] = g_modePage1B[j];
        for (j = 0; j < 8; j++, i++) p[i] = g_modePage1C[j];
        p[24] = 2; p[25] = 64;
        p[28] = (uint8_t)(cyl >> 8);
        p[29] = (uint8_t)cyl;
        break;
    }
    default:
        p[0] = 3;
        p[3] = 0x00;
        break;
    }
    MSC_BulkInProtocol(g_cbw.len);
}

uint32_t MSC_ReadMedia(uint32_t lba, uint32_t cnt, uint8_t * b)
{
    if (SDCard_ReadBlocks(b, lba, cnt) != SDCARD_OK) {
        MSC_DEBUG("MSC R fail L=%lu", lba);
        g_au8SenseKey[0] = 0x03;
        g_au8SenseKey[1] = 0x11;
        g_au8SenseKey[2] = 0x00;
        return 1;
    }
    return 0;
}
uint32_t MSC_WriteMedia(uint32_t lba, uint32_t cnt, uint8_t * b) {
    if (SDCard_WriteBlocks(b, lba, cnt) != SDCARD_OK) {
        MSC_DEBUG("MSC W fail L=%lu", lba);
        g_au8SenseKey[0] = 0x03;
        g_au8SenseKey[1] = 0x0C;
        g_au8SenseKey[2] = 0x00;
        return 1;
    }
    return 0;
}
void MSC_ProcessCmd(void) {
    uint32_t i = 0, lba = 0, cnt = 0;
    if (!HSUSBD_IS_ATTACHED()) { g_u8BulkState = MSC_BULK_NORMAL; return; }
    if (g_msc_io.op) {
        MSC_ServiceAsyncTransfer();
        return;
    }
    if (g_u8BulkState == MSC_BULK_ZLP_PENDING) {
        if (HSUSBD->EP[EPB].EPRSPCTL & HSUSBD_EP_RSPCTL_ZEROLEN) return;
        MSC_DEBUG("ZLP sent, send failed CSW");
        MSC_SendCSW();
        return;
    }
    if (g_u8BulkState == MSC_BULK_CSW_PENDING) {
        if (HSUSBD_GetEpStall(g_u8MscStallEp)) return;
        MSC_DEBUG("HALT cleared ep=%u, send failed CSW", (unsigned int)g_u8MscStallEp);
        MSC_SendCSW();
        return;
    }
    if (g_u8BulkState == MSC_BULK_NORMAL) {
        g_u8BulkState = MSC_BULK_OUT;
        MSC_ReceiveCBW(g_u32MassBase);
    }
    if (g_u8BulkState != MSC_BULK_CBW) return;
    if ( * (uint32_t * ) g_u32MassBase != 0x43425355) {
        MSC_DEBUG("invalid CBW signature=%08lX", (unsigned long)*(uint32_t *)g_u32MassBase);
        g_u8BulkState = MSC_BULK_NORMAL;
        return;
    }
    for (i = 0; i < 31; i++) * ((uint8_t * ) & g_cbw.sig + i) = * (uint8_t * )(g_u32MassBase + i);
    g_csw.tag = g_cbw.tag;
    MSC_DEBUG("CBW op=%02X tag=%08lX len=%lu dir=%s",
              (unsigned int)g_cbw.cdb[0], (unsigned long)g_cbw.tag,
              (unsigned long)g_cbw.len, (g_cbw.flags & 0x80) ? "IN" : "OUT");
    switch (g_cbw.cdb[0]) 
		{
        case 0x28: 
				{
            if (!MSC_RequireMedia()) {
                MSC_FailDataPhase(g_cbw.len);
                break;
            }
            lba = get_be32(&g_cbw.cdb[2]);
            cnt = ((uint32_t)g_cbw.cdb[7] << 8) | g_cbw.cdb[8];
            MSC_StartAsyncTransfer(MSC_IO_READ, lba, cnt);
            MSC_ServiceAsyncTransfer();
            break;
        }
        case 0x2A: 
					{					
            if (!MSC_RequireMedia()) {
                MSC_FailDataPhase(g_cbw.len);
                break;
            }
            lba = get_be32(&g_cbw.cdb[2]);
            cnt = ((uint32_t)g_cbw.cdb[7] << 8) | g_cbw.cdb[8];
            MSC_StartAsyncTransfer(MSC_IO_WRITE, lba, cnt);
            MSC_ServiceAsyncTransfer();
            break;
        }
        case 0x1E:
            g_u8Prevent = (g_cbw.cdb[4] & 0x01) ? 1 : 0;
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x2F:
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x00:
            MSC_AckCmd(0, MSC_RequireMedia() ? MSC_CSW_PASSED : MSC_CSW_FAILED);
            break;
        case 0x1B:
            /* START/STOP UNIT — 媒体状态变化, 正常应答 */
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x03:
            SCSI_ReqSense();
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x23:
            if (!MSC_RequireMedia()) {
                MSC_FailDataPhase(g_cbw.len);
                break;
            }
            SCSI_ReadFmt();
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x25:
            if (!MSC_RequireMedia()) {
                MSC_FailDataPhase(g_cbw.len);
                break;
            }
            SCSI_ReadCap();
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x12:
            memcpy((void * ) g_u32MassBase, g_inqID, 36);
            MSC_BulkInProtocol(g_cbw.len);
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x1A:
            SCSI_Mode6();
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x5A:
            SCSI_Mode10();
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        case 0x15:
        case 0x55:
            if (g_cbw.len > MSC_PROTOCOL_BUF_SIZE) {
                MSC_SetSense(0x05, 0x24, 0x00);
                MSC_FailDataPhase(g_cbw.len);
                break;
            }
            MSC_BulkOut(g_u32MassBase, g_cbw.len);
            MSC_AckCmd(0, MSC_CSW_PASSED);
            break;
        default:
            g_au8SenseKey[0] = 5;
            g_au8SenseKey[1] = 0x20;
            g_au8SenseKey[2] = 0;
            MSC_FailDataPhase(g_cbw.len);
            break;
    }
}
void MSC_ClassRequest(void) {
    uint16_t msc_iface = (HSUSBD->OPER & 0x04) ? 0x02 : 0x00;

    g_u8MscStart = 1;
    if (gUsbCmd.bmRequestType & 0x80) {
        if (gUsbCmd.bRequest == GET_MAX_LUN) 
				{
            if ((gUsbCmd.wValue == 0) && (gUsbCmd.wIndex == msc_iface) && (gUsbCmd.wLength == 1)) 
							{
                uint32_t ml = 0;
                HSUSBD_PrepareCtrlIn((uint8_t * ) & ml, 1);
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } 
							else 
								HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        } 
				else 
					HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
    } else {
        if (gUsbCmd.bRequest == BULK_ONLY_MASS_STORAGE_RESET) {
            if ((gUsbCmd.wValue == 0) && (gUsbCmd.wIndex == msc_iface) && (gUsbCmd.wLength == 0)) {
                HSUSBD_ResetDMA();
                if (g_msc_io.op && g_msc_io.sd_busy != MSC_BUF_INVALID)
                    g_u8MscAbortPending = 1;
                MSC_ResetAsyncTransfer();
                g_u8BulkState = MSC_BULK_NORMAL;
                g_u8MscClassResetEvent = 1;
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            } else HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        } else HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
    }
}
void Composite_ClassRequest(void) {
    uint8_t iface = (uint8_t)(gUsbCmd.wIndex & 0xFF);
    if ((HSUSBD->OPER & 0x04) && iface == 2) MSC_ClassRequest();
    else if ((HSUSBD->OPER & 0x04) && (iface == 0 || iface == 1)) UVC_HandleClassRequest(iface);
    else if (!(HSUSBD->OPER & 0x04) && iface == 0) MSC_ClassRequest();
    else HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
}
void USB_SetInterface(uint32_t alt) {
    if (gUsbCmd.wIndex == 1) {
        g_uvc_streaming = (alt == 1) ? 1 : 0;
        if (!g_uvc_streaming) {
            UVC_AbortFrame();
        }
    }
}
static void USB_HandleDetach(void) {
    UVC_ResetState();
    HSUSBD_DISABLE_USB();
    HSUSBD_SwReset();
    HSUSBD_ResetDMA();
    if (g_msc_io.op && g_msc_io.sd_busy != MSC_BUF_INVALID)
        g_u8MscAbortPending = 1;
    MSC_ResetAsyncTransfer();
    g_u8UsbStarted = 0;
    g_u8MscStart = 0;
    g_u8BulkState = MSC_BULK_NORMAL;
    g_u8MscConfigured = 0;
    g_uvc_configured = 0;
    g_uvc_streaming = 0;
    g_u8IsHS = 0;
    g_u8UsbDetachEvent = 1;
    g_u8MscReleasePending = 1;
    g_u8MscReenumPending = 0;
    g_u8MscMediaReady = 0;
    g_u8MscUnitAttention = 0;
    g_TotalSectors = 0;
}


void USBD20_IRQHandler(void) __attribute__ ((section(".ARM.__at_0x00006400")));
void USBD20_IRQHandler(void)
{
    __IO uint32_t IrqStL, IrqSt;
    IrqStL = HSUSBD -> GINTSTS & HSUSBD -> GINTEN;
    if (!IrqStL) {
        UVC_HandleTxIrq(0u);
        return;
    }
	
    if (IrqStL & HSUSBD_GINTSTS_USBIF_Msk) 
		{
        IrqSt = HSUSBD->BUSINTSTS & HSUSBD->BUSINTEN;
        if (IrqSt & HSUSBD_BUSINTSTS_SOFIF_Msk)
				{
					HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SOFIF_Msk);
				}
        if (IrqSt & HSUSBD_BUSINTSTS_RSTIF_Msk)
				{
            UVC_ResetState();
            HSUSBD_SwReset();
            g_u8MscStart = 0;
            g_u8BulkState = MSC_BULK_NORMAL;
            g_u8MscConfigured = 0;
            g_uvc_configured = 0;
            g_uvc_streaming = 0;
            g_u8BusReset = 1;
            g_u8IsHS = 0;
            g_u8MscReenumPending = 1;
            g_u8MscMediaReady = 0;
            g_u8MscUnitAttention = 0;
            g_TotalSectors = 0;
            HSUSBD_ResetDMA();
            if (g_msc_io.op && g_msc_io.sd_busy != MSC_BUF_INVALID)
                g_u8MscAbortPending = 1;
            MSC_ResetAsyncTransfer();
            HSUSBD -> EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
            HSUSBD -> EP[EPB].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
            HSUSBD -> EP[EPC].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
            if (HSUSBD -> OPER & 0x04) EpInitHS();
            else EpInitFS();
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            HSUSBD_SET_ADDR(0);
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk|HSUSBD_BUSINTEN_RESUMEIEN_Msk|HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RSTIF_Msk);
            HSUSBD_CLR_CEP_INT_FLAG(0x1FFC);
        }
        if (IrqSt & HSUSBD_BUSINTSTS_RESUMEIF_Msk) {
            g_u8BusResume = 1;
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RESUMEIF_Msk);
        }
        if (IrqSt & HSUSBD_BUSINTSTS_SUSPENDIF_Msk) {
            g_u8BusSuspend = 1;
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk | HSUSBD_BUSINTEN_RESUMEIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SUSPENDIF_Msk);
        }
        if (IrqSt & HSUSBD_BUSINTSTS_DMADONEIF_Msk) 
				{
            g_hsusbd_DmaDone = 1;
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_DMADONEIF_Msk);
            if (!(HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk)) {
                if (g_u8BulkState == MSC_BULK_OUT) g_u8BulkState = MSC_BULK_CBW;
                HSUSBD_ENABLE_EP_INT(EPC, HSUSBD_EPINTEN_RXPKIEN_Msk);
            }
            if ((HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk) && g_hsusbd_ShortPacket) {
                HSUSBD->EP[EPB].EPRSPCTL = (HSUSBD->EP[EPB].EPRSPCTL & 0x10) | HSUSBD_EP_RSPCTL_SHORTTXEN;
                g_hsusbd_ShortPacket = 0;
            }
        }
        if (IrqSt & HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk)
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk);
        if (IrqSt & HSUSBD_BUSINTSTS_VBUSDETIF_Msk) {
            g_u8BusVbus = 1;
            if (HSUSBD_IS_ATTACHED()) {
                HSUSBD_ENABLE_USB();
            } else {
                USB_HandleDetach();
            }
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_VBUSDETIF_Msk);
        }
        if (IrqSt & HSUSBD_BUSINTSTS_HISPDIF_Msk) {
            g_u8IsHS = 1;
            g_u8BusSpeed = 1;
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_HISPDIF_Msk);
        }
    }
    if (IrqStL & HSUSBD_GINTSTS_CEPIF_Msk) 
		{
        IrqSt = HSUSBD -> CEPINTSTS & HSUSBD -> CEPINTEN;
        if (IrqSt & HSUSBD_CEPINTSTS_SETUPTKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPTKIF_Msk);
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_SETUPPKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPPKIF_Msk);
            HSUSBD_ProcessSetupPacket();
            g_u8CepSetup = 1;
            g_u8CepSetupReq = gUsbCmd.bRequest;
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_OUTTKIF_Msk) {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_OUTTKIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_INTKIF_Msk) 
				{
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
            g_u8CepIntkCnt++;
            if (!(IrqSt & HSUSBD_CEPINTSTS_STSDONEIF_Msk)) {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk);
                HSUSBD_CtrlIn();
                g_u8CepCtrlInCnt++;
            } else {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            }
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_PINGIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_PINGIF_Msk);
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_TXPKIF_Msk) 
				{
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
            g_u8CepTxDoneCnt++;
            if (g_hsusbd_CtrlInSize) {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else {
                if (g_hsusbd_CtrlZero == 1)
                    HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_ZEROLEN);
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            }
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_RXPKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_RXPKIF_Msk);
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            return;
        }				
	      if (IrqSt & HSUSBD_CEPINTSTS_NAKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_NAKIF_Msk);
            return;
        }	
        if (IrqSt & HSUSBD_CEPINTSTS_STALLIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STALLIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_ERRIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_ERRIF_Msk);
            return;
        }				
        if (IrqSt & HSUSBD_CEPINTSTS_STSDONEIF_Msk) {
            HSUSBD_UpdateDeviceState();
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            g_u8CepStsDone = 1;
            return;
        }
        if (IrqSt & HSUSBD_CEPINTSTS_BUFFULLIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFFULLIF_Msk);
            return;
        }

        if (IrqSt & HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk);
            return;
        }				
    }
		
		
    /* bulk in Video Streaming*/
    if (IrqStL & HSUSBD_GINTSTS_EPAIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPA].EPINTSTS & HSUSBD->EP[EPA].EPINTEN;
        UVC_HandleTxIrq(IrqSt);
    }
    /* bulk in out */
    if (IrqStL & HSUSBD_GINTSTS_EPBIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPB].EPINTSTS & HSUSBD->EP[EPB].EPINTEN;
        HSUSBD_ENABLE_EP_INT(EPB, 0);
        HSUSBD_CLR_EP_INT_FLAG(EPB, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPCIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPC].EPINTSTS & HSUSBD->EP[EPC].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPC, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPDIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPD].EPINTSTS & HSUSBD->EP[EPD].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPD, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPEIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPE].EPINTSTS & HSUSBD->EP[EPE].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPE, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPFIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPF].EPINTSTS & HSUSBD->EP[EPF].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPF, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPGIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPG].EPINTSTS & HSUSBD->EP[EPG].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPG, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPHIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPH].EPINTSTS & HSUSBD->EP[EPH].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPH, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPIIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPI].EPINTSTS & HSUSBD->EP[EPI].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPI, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPJIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPJ].EPINTSTS & HSUSBD->EP[EPJ].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPJ, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPKIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPK].EPINTSTS & HSUSBD->EP[EPK].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPK, IrqSt);
    }

    if (IrqStL & HSUSBD_GINTSTS_EPLIF_Msk)
    {
        IrqSt = HSUSBD->EP[EPL].EPINTSTS & HSUSBD->EP[EPL].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPL, IrqSt);
    }
}

/* ===== USB_Composite_Init ===== */
void USB_Composite_Init(void) {
    uint32_t volatile i;

    /* Unlock protected registers for USB PHY config */
    SYS_UnlockReg();

    /* Select HSUSBD mode (not OTG host) */
    SYS->USBPHY &= ~SYS_USBPHY_HSUSBROLE_Msk;

    /* Enable HS USB PHY */
    SYS->USBPHY = (SYS->USBPHY &
        ~(SYS_USBPHY_HSUSBROLE_Msk | SYS_USBPHY_HSUSBACT_Msk))
        | SYS_USBPHY_HSUSBEN_Msk;

    /* Wait >10us for PHY to stabilize */
    for (i = 0; i < 0x2000; i++);

    /* Activate HS USB PHY */
    SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;

    /* Enable HSUSBD module clock */
    CLK_EnableModuleClock(HSUSBD_MODULE);
    SYS_ResetModule(HSUSBD_RST);

    /* Re-lock protected registers */
    SYS_LockReg();

    UVC_Init();

    g_u32MassBase = (uint32_t)g_msc_protocol_buf;
    MSC_ResetAsyncTransfer();
    g_csw.sig = 0x53425355;
    HSUSBD_Open(&gsHSInfo, Composite_ClassRequest, USB_SetInterface);
    HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk|HSUSBD_GINTEN_CEPIEN_Msk|HSUSBD_GINTEN_EPAIEN_Msk|HSUSBD_GINTEN_EPBIEN_Msk|HSUSBD_GINTEN_EPCIEN_Msk);
    HSUSBD_ENABLE_BUS_INT(USB_BUS_INT_COMMON | HSUSBD_BUSINTEN_DMADONEIEN_Msk | HSUSBD_BUSINTEN_RESUMEIEN_Msk);
    
    HSUSBD_SET_ADDR(0);
		HSUSBD_SetEpBufAddr(CEP, CEP_BUF_BASE, CEP_BUF_LEN);
    HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk|HSUSBD_CEPINTEN_STSDONEIEN_Msk);
    EpInitHS();

    /* Enable USBD20 interrupt in NVIC */
    NVIC_EnableIRQ(USBD20_IRQn);

    if (HSUSBD_IS_ATTACHED()) {
        HSUSBD_Start();
        g_u8UsbStarted = 1;
        g_u8UsbConnectEvent = 1;
    }

    USB_DEBUG("USB init done");
}

/* ===== USB_Composite_Service ===== */
void USB_Composite_Service(void) {
    if (g_u8MscAbortPending) {
        g_u8MscAbortPending = 0;
        SDCard_AbortTransfer();
        MSC_ResetAsyncTransfer();
    }
    if (g_u8BusVbus) {
        g_u8BusVbus = 0;
        USB_DEBUG("VBUS %s", HSUSBD_IS_ATTACHED() ? "ON" : "OFF");
    }
    if (g_u8UsbDetachEvent) {
        g_u8UsbDetachEvent = 0;
        USB_DEBUG("DETACH phy=0x%08lX", (unsigned long)HSUSBD->PHYCTL);
    }
    if (g_u8UsbConnectEvent) {
        g_u8UsbConnectEvent = 0;
        USB_DEBUG("CONNECT phy=0x%08lX", (unsigned long)HSUSBD->PHYCTL);
    }
    if (g_u8BusReset) {
        g_u8BusReset = 0;
        USB_DEBUG("BUS RESET");
    }
    if (g_u8BusSpeed) {
        g_u8BusSpeed = 0;
        USB_DEBUG("Speed=%s", g_u8IsHS ? "HS" : "FS");
    }
    if (g_u8BusResume) {
        g_u8BusResume = 0;
        USB_DEBUG("RESUME");
    }
    if (g_u8BusSuspend) {
        g_u8BusSuspend = 0;
        USB_DEBUG("SUSPEND");
    }
    if (g_u8MscClassResetEvent) {
        g_u8MscClassResetEvent = 0;
        MSC_DEBUG("BULK-ONLY RESET");
    }
    if (g_u8MscReenumPending) {
        if (g_u8MscReenumPending == 1) {
            g_u32MscReenumTick = GetTick();
            g_u8MscReenumPending = 2;
            MSC_DEBUG("bus reset, wait %u ms for re-enumeration",
                      (unsigned int)MSC_REENUM_GRACE_MS);
        } else if (!g_hsusbd_Configured &&
                   (int32_t)(GetTick() - g_u32MscReenumTick) >= MSC_REENUM_GRACE_MS) {
            g_u8MscReenumPending = 0;
            SDCard_ReleaseFromMSC();
            MSC_DEBUG("re-enumeration timeout, release media to FATFS");
        }
    }
    if (g_u8CepSetup) {
        g_u8CepSetup = 0;
        USB_DEBUG("SETUP req=0x%02X v=0x%04X i=0x%04X l=%u",
                  g_u8CepSetupReq, gUsbCmd.wValue, gUsbCmd.wIndex,
                  (unsigned int)gUsbCmd.wLength);
    }
    if (g_u8CepIntkCnt) {
        USB_DEBUG("  IN_TOKEN x%u", g_u8CepIntkCnt);
        g_u8CepIntkCnt = 0;
    }
    if (g_u8CepCtrlInCnt) {
        USB_DEBUG("  CTRL_IN x%u (remain=%u)", g_u8CepCtrlInCnt,
                  (unsigned int)g_hsusbd_CtrlInSize);
        g_u8CepCtrlInCnt = 0;
    }
    if (g_u8CepTxDoneCnt) {
        USB_DEBUG("  TX_DONE x%u", g_u8CepTxDoneCnt);
        g_u8CepTxDoneCnt = 0;
    }
    if (g_u8CepStsDone) {
        g_u8CepStsDone = 0;
        USB_DEBUG("  STATUS_DONE addr=%u cfg=%u",
                  (unsigned int)g_hsusbd_UsbAddr,
                  (unsigned int)g_hsusbd_Configured);
    }
    if (g_u8MscReleasePending) {
        g_u8MscReleasePending = 0;
        SDCard_ReleaseFromMSC();
    }
    if (!HSUSBD_IS_ATTACHED()) {
        if (g_u8UsbStarted || g_u8MscConfigured || g_hsusbd_Configured ||
            (HSUSBD->PHYCTL & HSUSBD_PHYCTL_DPPUEN_Msk)) {
            USB_HandleDetach();
        }
        return;
    }
    if (!g_u8UsbStarted || !(HSUSBD->PHYCTL & HSUSBD_PHYCTL_DPPUEN_Msk)) {
        HSUSBD_Start();
        g_u8UsbStarted = 1;
        g_u8UsbConnectEvent = 1;
    }

    if (g_u8MscConfigured) {
        if (!g_hsusbd_Configured) {
            MSC_AbortAsyncTransfer();
            g_u8MscStart = 0;
            g_u8BulkState = MSC_BULK_NORMAL;
            g_u8MscConfigured = 0;
            g_u8MscReenumPending = 0;
            SDCard_ReleaseFromMSC();
            return;
        }
        MSC_UpdateMediaState();
        MSC_ProcessCmd();
    } else {
        if (g_hsusbd_Configured) {
            const sdcard_info_t *info;

            SDCard_AcquireForMSC();
            g_u8MscMediaReady = SDCard_IsReady();
            info = SDCard_GetInfo();
            g_TotalSectors = (g_u8MscMediaReady && info) ? info->total_sectors : 0;
            g_u8MscUnitAttention = 0;
            g_u8MscStart = 1;
            g_u8BulkState = MSC_BULK_NORMAL;
            g_u8MscConfigured = 1;
            g_u8MscReenumPending = 0;
            g_uvc_configured = 1;
            USB_DEBUG("CONFIGURED");
        }
    }
}

uint8_t USB_Composite_IsUVCReady(void)
{
    if (!HSUSBD_IS_ATTACHED()) {
        return 0u;
    }
    if (!(HSUSBD->OPER & 0x04)) {
        return 0u;
    }
    if (!g_hsusbd_Configured || !g_uvc_configured || !g_uvc_streaming) {
        return 0u;
    }
    return 1u;
}

usb_ui_state_t USB_Composite_GetUIState(void)
{
    if (!HSUSBD_IS_ATTACHED()) {
        return USB_UI_DETACHED;
    }
    if (SDCard_IsOwnedByMSC()) {
        return USB_UI_MSC_OWNING_SD;
    }
    if (USB_Composite_IsUVCReady()) {
        return USB_UI_UVC_STREAMING;
    }
    if (g_hsusbd_Configured) {
        return USB_UI_CONFIGURED;
    }
    return USB_UI_ATTACHED;
}
