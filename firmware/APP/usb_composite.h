#ifndef __USB_COMPOSITE_H
#define __USB_COMPOSITE_H

#include "M480.h"

/* ── Vendor/Product IDs ── */
#define USBD_VID                    0x0416
#define USBD_PID                    0x0480

/* ── DMA ── */
#define USBD_MAX_DMA_LEN            0x1000
#define USBD_SECTOR_SIZE            512

/* ── EP max packet size ── */
#define CEP_MAX_PKT_SIZE            64
#define CEP_OTHER_MAX_PKT_SIZE      64

/* MSC bulk endpoints (EPB=1, EPC=2) */
#define MSC_BULK_IN_EP_NUM          0x02
#define MSC_BULK_OUT_EP_NUM         0x02

#define EPA_MAX_PKT_SIZE            1024
#define EPA_OTHER_MAX_PKT_SIZE      64
#define EPB_MAX_PKT_SIZE            512
#define EPB_OTHER_MAX_PKT_SIZE      64
#define EPC_MAX_PKT_SIZE            512
#define EPC_OTHER_MAX_PKT_SIZE      64
#define EPD_MAX_PKT_SIZE            64
#define EPD_OTHER_MAX_PKT_SIZE      64

/* ── EP buffer base/len ── */
#define CEP_BUF_BASE                0x0000
#define CEP_BUF_LEN                 CEP_MAX_PKT_SIZE

#define EPA_BUF_BASE                0x0200  /* UVC isoch IN 1K*/
#define EPA_BUF_LEN                 EPA_MAX_PKT_SIZE

#define EPB_BUF_BASE                0x0600  /* MSC bulk IN */
#define EPB_BUF_LEN                 EPB_MAX_PKT_SIZE

#define EPC_BUF_BASE                0x0800  /* MSC bulk OUT */
#define EPC_BUF_LEN                 EPC_MAX_PKT_SIZE

//#define EPD_BUF_BASE                0x0800  /* VC intr IN (opt) */
//#define EPD_BUF_LEN                 EPD_MAX_PKT_SIZE

/* ── Power / wakeup ── */
#define USBD_SELF_POWERED           0
#define USBD_REMOTE_WAKEUP          0
#define USBD_MAX_POWER              50      /* 100 mA */

/* ── IAD (not in BSP) ── */
#define DESC_IAD                    0x0B
#define LEN_IAD                     8

/* ── Class requests ── */
#define BULK_ONLY_MASS_STORAGE_RESET 0xFF
#define GET_MAX_LUN                 0xFE

/* ── UVC class requests ── */
#define UVC_VC_REQUEST_ERROR_CONTROL     0x02
#define UVC_VS_PROBE_CONTROL              0x01
#define UVC_VS_COMMIT_CONTROL             0x02
#define UVC_VS_STILL_PROBE_CONTROL        0x03
#define UVC_VS_STILL_COMMIT_CONTROL       0x04

/* ── API ── */
typedef enum {
    USB_UI_DETACHED = 0,
    USB_UI_ATTACHED,
    USB_UI_CONFIGURED,
    USB_UI_UVC_STREAMING,
    USB_UI_MSC_OWNING_SD,
} usb_ui_state_t;

void USB_Composite_Init(void);
void USB_Composite_Service(void);
uint8_t USB_Composite_IsUVCReady(void);
usb_ui_state_t USB_Composite_GetUIState(void);

#endif
