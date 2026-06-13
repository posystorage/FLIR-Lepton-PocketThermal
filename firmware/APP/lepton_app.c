#include "lepton_app.h"
#include "lepton.h"
#include "image_proc.h"
#include "temp_measure.h"
#include "sys_tick.h"
#include "debug.h"

#define LEPTON_INIT_RETRY_MS  5000u
#define LEPTON_FFC_POLL_MS     100u
#define LEPTON_FFC_DISPLAY_MS  700u

#define LEPTON_FFC_NEVER_COMMANDED 0u
#define LEPTON_FFC_IMMINENT        1u
#define LEPTON_FFC_IN_PROCESS      2u
#define LEPTON_FFC_DONE            3u

#define LEPTON_SYS_STATUS_READY    0u
#define LEPTON_SYS_STATUS_BUSY     1u
#define LEPTON_SYS_STATUS_COLLECT  2u

static uint8_t g_lepton_app_inited = 0;
static uint8_t g_pipeline_busy = 0;
static uint8_t g_image_proc_inited = 0;
static uint8_t g_manual_ffc_requested = 0;
static uint8_t g_ffc_in_progress = 0;
static uint32_t g_next_lepton_retry_ms = 0;
static uint32_t g_next_ffc_poll_ms = 0;
static uint32_t g_ffc_display_until_ms = 0;

static void Lepton_App_PreparePipeline(void)
{
    if (!g_image_proc_inited) {
        image_proc_init();
        g_image_proc_inited = 1;
    }
}

static void Lepton_App_ShowFFC(uint32_t now)
{
    g_ffc_in_progress = 1u;
    g_ffc_display_until_ms = now + LEPTON_FFC_DISPLAY_MS;
}

static uint8_t Lepton_App_TryInit(void)
{
    uint8_t rbfo_valid = 0;
    const lep_rbfo_t *rbfo;

    Lepton_App_PreparePipeline();
    g_pipeline_busy = 0;
    g_lepton_app_inited = 0;
    g_ffc_in_progress = 0;
    g_ffc_display_until_ms = 0;

    if (Lepton_Init() == 0) {
        g_next_lepton_retry_ms = GetTick() + LEPTON_INIT_RETRY_MS;
        IMG_DEBUG("lepton init failed, retry in %u ms", LEPTON_INIT_RETRY_MS);
        return 0;
    }

    rbfo = Lepton_GetRBFO(&rbfo_valid);
    temp_init(rbfo, rbfo_valid);

    g_lepton_app_inited = 1;
    IMG_DEBUG("lepton app init ok");
    return 1;
}

static void Lepton_App_PollFFCState(uint32_t now)
{
    uint16_t state[2] = {0u, 0u};
    uint16_t status[2] = {0u, 0u};
    int ret;

    if ((int32_t)(now - g_next_ffc_poll_ms) < 0) {
        return;
    }
    g_next_ffc_poll_ms = now + LEPTON_FFC_POLL_MS;

    ret = lep_get(LEP_CMD_SYS_FFC_STATUS_GET, status, 2u);
    if (ret == LEP_OK) {
        if (status[0] == LEPTON_SYS_STATUS_BUSY ||
            status[0] == LEPTON_SYS_STATUS_COLLECT) {
            Lepton_App_ShowFFC(now);
            return;
        }
    } else if (ret == LEP_ERR_TIMEOUT) {
        Lepton_App_ShowFFC(now);
        return;
    }

    ret = lep_get(LEP_CMD_SYS_FFC_STATE_GET, state, 2u);
    if (ret != LEP_OK) {
        if (ret == LEP_ERR_TIMEOUT) {
            Lepton_App_ShowFFC(now);
            return;
        }
        if ((int32_t)(now - g_ffc_display_until_ms) >= 0) {
            g_ffc_in_progress = 0u;
        }
        return;
    }

    if (state[0] == LEPTON_FFC_IMMINENT ||
        state[0] == LEPTON_FFC_IN_PROCESS) {
        Lepton_App_ShowFFC(now);
    } else if ((int32_t)(now - g_ffc_display_until_ms) >= 0) {
        g_ffc_in_progress = 0u;
    }
}

static void Lepton_App_ServiceManualFFC(void)
{
    int ret;

    if (!g_manual_ffc_requested || g_pipeline_busy) {
        return;
    }

    g_manual_ffc_requested = 0u;
    ret = lep_run(LEP_CMD_SYS_FFC_RUN);
    if (ret == LEP_OK) {
        uint32_t now = GetTick();
        Lepton_App_ShowFFC(now);
        g_next_ffc_poll_ms = now + LEPTON_FFC_POLL_MS;
        IMG_DEBUG("manual ffc run");
    } else {
        IMG_DEBUG("manual ffc err=%d", ret);
    }
}

void Lepton_App_Init(void)
{
    g_next_lepton_retry_ms = 0;
    (void)Lepton_App_TryInit();
}

void Lepton_Frame_Service(void)
{
    const uint16_t (*raw14)[80];
    uint32_t now;

    if (!g_lepton_app_inited) {
        now = GetTick();
        if ((int32_t)(now - g_next_lepton_retry_ms) >= 0) {
            (void)Lepton_App_TryInit();
        }
        return;
    }

    now = GetTick();
    Lepton_App_PollFFCState(now);
    Lepton_App_ServiceManualFFC();

    if (g_pipeline_busy) {
        if (image_pipeline_run(0)) {
            Lepton_SetConsumerBusy(0);
            g_pipeline_busy = 0;
        }
        return;
    }

    if (Lepton_Capture_Service() == 0)
        return;

    raw14 = Lepton_GetReadyFrame();
    if (raw14 == 0)
        return;

    g_pipeline_busy = 1;
    Lepton_SetConsumerBusy(1);

    if (image_pipeline_run(raw14)) {
        Lepton_SetConsumerBusy(0);
        g_pipeline_busy = 0;
    }
    Lepton_ReleaseReadyFrame();

    if ((Lepton_GetBusyDropCount() & 0x1Fu) == 1u) {
        IMG_DEBUG("lepton busy drops=%lu", Lepton_GetBusyDropCount());
    }
}

void Lepton_App_RequestManualFFC(void)
{
    g_manual_ffc_requested = 1u;
}

uint8_t Lepton_App_IsFFCInProgress(void)
{
    if (g_ffc_in_progress &&
        (int32_t)(GetTick() - g_ffc_display_until_ms) >= 0) {
        g_ffc_in_progress = 0u;
    }
    return g_ffc_in_progress;
}
