#ifndef __USB_UVC_H
#define __USB_UVC_H

#include "M480.h"

/* UVC class-specific request constants */
#define UVC_SET_CUR     0x01
#define UVC_GET_CUR     0x81
#define UVC_GET_MIN     0x82
#define UVC_GET_MAX     0x83
#define UVC_GET_RES     0x84
#define UVC_GET_LEN     0x85
#define UVC_GET_INFO    0x86
#define UVC_GET_DEF     0x87

/* VS control selectors */
#define VS_PROBE_CONTROL           0x01
#define VS_COMMIT_CONTROL          0x02
#define VS_STILL_PROBE_CONTROL     0x03
#define VS_STILL_COMMIT_CONTROL    0x04

/* VC control selectors (per interface) */
#define VC_VIDEO_POWER_MODE_CONTROL    0x01
#define VC_REQUEST_ERROR_CODE_CONTROL  0x02

/* UVC streaming states */
#define UVC_STREAM_OFF   0
#define UVC_STREAM_ON    1

/* ── Probe/Commit structure (VS_PROBE_AND_COMMIT) ── */
typedef struct __attribute__((packed)) {
    uint16_t bmHint;
    uint8_t  bFormatIndex;
    uint8_t  bFrameIndex;
    uint32_t dwFrameInterval;
    uint16_t wKeyFrameRate;
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;
    uint32_t dwMaxPayloadTransferSize;
    uint32_t dwClockFrequency;
    uint8_t  bmFramingInfo;
    uint8_t  bPreferedVersion;
    uint8_t  bMinVersion;
    uint8_t  bMaxVersion;
} uvc_probe_commit_t;

/* ── API ── */
void UVC_Init(void);
void UVC_HandleClassRequest(uint8_t interface);
void UVC_ResetState(void);
uint8_t UVC_IsReady(void);
uint8_t UVC_BeginGrayFrame(uint8_t gray[240][320]);
void UVC_HandleTxIrq(uint32_t irq_status);
void UVC_AbortFrame(void);
uint8_t UVC_IsFrameBusy(void);

#endif
