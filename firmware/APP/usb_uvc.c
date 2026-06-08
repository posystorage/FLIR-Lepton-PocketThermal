#include <string.h>
#include "usb_composite.h"
#include "usb_uvc.h"
#include "color_palette.h"
#include "debug.h"

#define UVC_FRAME_W                 320u
#define UVC_FRAME_H                 240u
#define UVC_FRAME_PIXELS            (UVC_FRAME_W * UVC_FRAME_H)
#define UVC_PAYLOAD_HEADER_SIZE     2u
#define UVC_PAYLOAD_VIDEO_BYTES     (508u+512u)
#define UVC_PAYLOAD_PAIRS           (UVC_PAYLOAD_VIDEO_BYTES / 4u)
#define UVC_TX_EP_INT_MASK          (HSUSBD_EPINTEN_BUFEMPTYIEN_Msk | \
                                     HSUSBD_EPINTEN_TXPKIEN_Msk | \
                                     HSUSBD_EPINTEN_INTKIEN_Msk | \
                                     HSUSBD_EPINTEN_ERRIEN_Msk | \
                                     HSUSBD_EPINTEN_STALLIEN_Msk)
#define UVC_TX_EP_SERVICE_FLAGS     (HSUSBD_EPINTSTS_BUFEMPTYIF_Msk | \
                                     HSUSBD_EPINTSTS_TXPKIF_Msk | \
                                     HSUSBD_EPINTSTS_INTKIF_Msk)
#define UVC_TX_EP_ERROR_FLAGS       (HSUSBD_EPINTSTS_ERRIF_Msk | \
                                     HSUSBD_EPINTSTS_STALLIF_Msk)
#define UVC_TX_EP_CLEAR_BEFORE_SEND (HSUSBD_EPINTSTS_TXPKIF_Msk | \
                                     HSUSBD_EPINTSTS_INTKIF_Msk | \
                                     HSUSBD_EPINTSTS_SHORTTXIF_Msk | \
                                     HSUSBD_EPINTSTS_ERRIF_Msk | \
                                     HSUSBD_EPINTSTS_STALLIF_Msk)

#ifndef UVC_ISR_PACKET_PRINTF
#define UVC_ISR_PACKET_PRINTF       0u
#endif

/* ──── Default probe/commit structure for 320x240 YUY2 @8fps ──── */
/* dwMaxVideoFrameSize = 320*240*2 = 153,600 */
/* dwMaxPayloadTransferSize = 512 (1 transaction per microframe ISOC) */
static uvc_probe_commit_t g_probe;
static uvc_probe_commit_t g_commit;
static uint16_t g_probe_commit_len = sizeof(uvc_probe_commit_t);
static uint8_t g_probe_commit_info = 0x03u;  /* GET and SET supported. */
static uint8_t g_vc_get_only_info = 0x01u;
static uint8_t g_vc_get_set_info = 0x03u;
static uint8_t g_vc_control_len_1 = 1u;
static uint8_t g_vc_power_mode = 0x01u;
static uint8_t g_vc_error_code;
static uint8_t g_uvc_commit_valid;
static uint8_t (*g_uvc_gray)[UVC_FRAME_W];
static const yuv888_t *g_uvc_palette_yuv;
static volatile uint32_t g_uvc_pair_pos;
static volatile uint8_t g_uvc_frame_busy;
static volatile uint8_t g_uvc_fid;

typedef char uvc_probe_commit_size_must_be_34[(sizeof(uvc_probe_commit_t) == 34u) ? 1 : -1];

static uint8_t UVC_ServiceFrameFromIsr(void);

static void UVC_InitDefaults(uvc_probe_commit_t *p)
{
    memset(p, 0, sizeof(uvc_probe_commit_t));
    p->bFormatIndex = 1;
    p->bFrameIndex = 1;
    p->dwFrameInterval = 1250000;      /* 1,250,000 * 100ns = 125ms = 8fps */
    p->wKeyFrameRate = 0;
    p->wPFrameRate = 0;
    p->wCompQuality = 0;
    p->wCompWindowSize = 0;
    p->wDelay = 0;
    p->dwMaxVideoFrameSize = 153600;
    p->dwMaxPayloadTransferSize = 1024;
    p->dwClockFrequency = 48000000u;
    p->bmFramingInfo = 0;
    p->bPreferedVersion = 0x01;
    p->bMinVersion = 0x01;
    p->bMaxVersion = 0x01;
}

/* ──── Handle VS probe/commit CUR/GET requests ──── */
static void UVC_HandleVS_ProbeCommit(uint8_t request, uint8_t is_commit)
{
    uvc_probe_commit_t *target = is_commit ? &g_commit : &g_probe;
    uint16_t out_len;

    switch (request) {
    case UVC_SET_CUR:
        out_len = gUsbCmd.wLength;
        if (out_len > sizeof(uvc_probe_commit_t)) {
            out_len = sizeof(uvc_probe_commit_t);
        }
        HSUSBD_CtrlOut((uint8_t *)target, out_len);
        if (is_commit) {
            g_uvc_commit_valid = 1u;
        }
        break;

    case UVC_GET_CUR:
    case UVC_GET_MIN:
    case UVC_GET_MAX:
    case UVC_GET_RES:
    case UVC_GET_DEF:
        HSUSBD_PrepareCtrlIn((uint8_t *)target, sizeof(uvc_probe_commit_t));
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;

    case UVC_GET_LEN:
        HSUSBD_PrepareCtrlIn((uint8_t *)&g_probe_commit_len, sizeof(g_probe_commit_len));
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;

    case UVC_GET_INFO:
        HSUSBD_PrepareCtrlIn(&g_probe_commit_info, sizeof(g_probe_commit_info));
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        break;

    default:
        g_vc_error_code = 0x07u; /* Invalid control. */
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
        break;
    }
}

/* ──── UVC_HandleClassRequest — called from composite ClassRequest ──── */
void UVC_HandleClassRequest(uint8_t interface)
{
    uint8_t cs = (uint8_t)(gUsbCmd.wValue >> 8);
    uint8_t request = gUsbCmd.bRequest;

    if (interface == 0) {
        /* VideoControl interface */
        switch (cs) {
        case VC_VIDEO_POWER_MODE_CONTROL:
            if (request == UVC_SET_CUR) {
                HSUSBD_CtrlOut(&g_vc_power_mode, sizeof(g_vc_power_mode));
            } else if (request == UVC_GET_CUR || request == UVC_GET_DEF) {
                HSUSBD_PrepareCtrlIn(&g_vc_power_mode, sizeof(g_vc_power_mode));
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else if (request == UVC_GET_INFO) {
                HSUSBD_PrepareCtrlIn(&g_vc_get_set_info, sizeof(g_vc_get_set_info));
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else if (request == UVC_GET_LEN) {
                HSUSBD_PrepareCtrlIn(&g_vc_control_len_1, sizeof(g_vc_control_len_1));
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else {
                g_vc_error_code = 0x07u;
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            }
            break;

        case VC_REQUEST_ERROR_CODE_CONTROL:
            if (request == UVC_GET_CUR) {
                HSUSBD_PrepareCtrlIn(&g_vc_error_code, 1);
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else if (request == UVC_GET_INFO) {
                HSUSBD_PrepareCtrlIn(&g_vc_get_only_info, sizeof(g_vc_get_only_info));
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else if (request == UVC_GET_LEN) {
                HSUSBD_PrepareCtrlIn(&g_vc_control_len_1, sizeof(g_vc_control_len_1));
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            } else {
                g_vc_error_code = 0x07u;
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            }
            break;
        default:
            g_vc_error_code = 0x07u;
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            break;
        }
    } else if (interface == 1) {
        /* VideoStreaming interface */
        switch (cs) {
        case VS_PROBE_CONTROL:
            UVC_HandleVS_ProbeCommit(request, 0);
            break;
        case VS_COMMIT_CONTROL:
            UVC_HandleVS_ProbeCommit(request, 1);
            break;
        default:
            g_vc_error_code = 0x07u;
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
            break;
        }
    } else {
        g_vc_error_code = 0x07u;
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
    }
}

/* ──── Initialize UVC defaults ──── */
void UVC_Init(void)
{
    UVC_InitDefaults(&g_probe);
    UVC_InitDefaults(&g_commit);
    UVC_ResetState();
    USB_DEBUG("UVC init done");
}

void UVC_ResetState(void)
{
    g_uvc_commit_valid = 0u;
    g_vc_error_code = 0u;
    UVC_AbortFrame();
}

uint8_t UVC_IsReady(void)
{
    return (USB_Composite_IsUVCReady() && g_uvc_commit_valid);
}

uint16_t g_uvc_Pnums = 0;
void UVC_AbortFrame(void)
{
    g_uvc_gray = 0;
    g_uvc_palette_yuv = 0;
    g_uvc_pair_pos = 0;
		g_uvc_Pnums = 0;
    g_uvc_frame_busy = 0;
    if (HSUSBD_IS_ATTACHED()) {
        HSUSBD_ENABLE_EP_INT(EPA, 0);
        HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
    }
}

uint8_t UVC_IsFrameBusy(void)
{
    return g_uvc_frame_busy;
}

uint8_t UVC_BeginGrayFrame(uint8_t gray[UVC_FRAME_H][UVC_FRAME_W])
{
    if (!UVC_IsReady() || gray == 0) {
        UVC_AbortFrame();
        return 0u;
    }

    if (g_uvc_frame_busy) {
        return 0u;
    }

    g_uvc_gray = gray;
    g_uvc_palette_yuv = palette_get_yuv();
    g_uvc_pair_pos = 0;
		g_uvc_Pnums = 0;
    g_uvc_fid ^= 1u;
    g_uvc_frame_busy = 1u;
#if (UVC_ISR_PACKET_PRINTF)
        printf("b");
#endif
    HSUSBD_CLR_EP_INT_FLAG(EPA, UVC_TX_EP_CLEAR_BEFORE_SEND);
    HSUSBD_ENABLE_EP_INT(EPA, UVC_TX_EP_INT_MASK);
    NVIC_SetPendingIRQ(USBD20_IRQn);
    return 1u;
}

static uint8_t UVC_ServiceFrameFromIsr(void)
{
    uint32_t pairs_left;
    uint32_t pairs_this_packet;
    uint32_t i;
    uint32_t pixel;
    uint32_t pair_pos;
    const uint8_t *gray;
    const yuv888_t *pal;
    const yuv888_t *c0;
    const yuv888_t *c1;
    uint8_t header;

    if (!g_uvc_frame_busy) {
        return 1u;
    }

    if (!UVC_IsReady() || g_uvc_gray == 0 || g_uvc_palette_yuv == 0) {
        UVC_AbortFrame();
        return 1u;
    }

    if ((g_uvc_pair_pos != 0u) &&
        !(HSUSBD_GET_EP_INT_FLAG(EPA) & HSUSBD_EPINTSTS_BUFEMPTYIF_Msk)) {
        return 0u;
    }

    pair_pos = g_uvc_pair_pos;
    pairs_left = (UVC_FRAME_PIXELS / 2u) - pair_pos;
    if (pairs_left == 0u) {
        UVC_AbortFrame();
        return 1u;
    }

    pairs_this_packet = (pairs_left > UVC_PAYLOAD_PAIRS) ? UVC_PAYLOAD_PAIRS : pairs_left;
    header = g_uvc_fid & 0x01u;
    if (pairs_this_packet == pairs_left) {
        header |= 0x02u;
    }

    HSUSBD_CLR_EP_INT_FLAG(EPA, HSUSBD_EPINTSTS_BUFEMPTYIF_Msk);
    HSUSBD->EP[EPA].EPDAT_BYTE = UVC_PAYLOAD_HEADER_SIZE;
    HSUSBD->EP[EPA].EPDAT_BYTE = header;
    gray = &g_uvc_gray[0][0];
    pal = g_uvc_palette_yuv;

    for (i = 0; i < pairs_this_packet; i++) {
        pixel = (pair_pos + i) * 2u;
        c0 = &pal[gray[pixel]];
        c1 = &pal[gray[pixel + 1u]];

        HSUSBD->EP[EPA].EPDAT_BYTE = c0->y;
        HSUSBD->EP[EPA].EPDAT_BYTE = (uint8_t)(((uint16_t)c0->u + (uint16_t)c1->u) >> 1);
        HSUSBD->EP[EPA].EPDAT_BYTE = c1->y;
        HSUSBD->EP[EPA].EPDAT_BYTE = (uint8_t)(((uint16_t)c0->v + (uint16_t)c1->v) >> 1);
    }

    g_uvc_pair_pos = pair_pos + pairs_this_packet;
    HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;
    HSUSBD_ENABLE_EP_INT(EPA, UVC_TX_EP_INT_MASK);
		g_uvc_Pnums++;

#if (UVC_ISR_PACKET_PRINTF)
		//printf("%X",g_uvc_Pnums);
#endif

    if (g_uvc_pair_pos >= (UVC_FRAME_PIXELS / 2u)) {
#if (UVC_ISR_PACKET_PRINTF)
        printf("e");
#endif
        g_uvc_frame_busy = 0u;
        g_uvc_gray = 0;
        g_uvc_palette_yuv = 0;
        return 1u;
    }

    return 0u;
}

void UVC_HandleTxIrq(uint32_t irq_status)
{
    uint32_t clear_flags;

    if (!g_uvc_frame_busy) {
        if (irq_status != 0u) {
            HSUSBD_CLR_EP_INT_FLAG(EPA, irq_status);
        }
        HSUSBD_ENABLE_EP_INT(EPA, 0);
        return;
    }

    if (irq_status & UVC_TX_EP_ERROR_FLAGS) {
        HSUSBD_CLR_EP_INT_FLAG(EPA, irq_status);
        UVC_AbortFrame();
        return;
    }

    clear_flags = irq_status & ~HSUSBD_EPINTSTS_BUFEMPTYIF_Msk;
    if (clear_flags != 0u) {
        HSUSBD_CLR_EP_INT_FLAG(EPA, clear_flags);
    }

    if ((irq_status == 0u) || (irq_status & UVC_TX_EP_SERVICE_FLAGS)) {
        (void)UVC_ServiceFrameFromIsr();
    }
}
