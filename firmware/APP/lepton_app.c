#include "lepton_app.h"
#include "lepton.h"
#include "image_proc.h"
#include "temp_measure.h"
#include "sys_tick.h"
#include "debug.h"

#define LEPTON_INIT_RETRY_MS  5000u

static uint8_t g_lepton_app_inited = 0;
static uint8_t g_pipeline_busy = 0;
static uint8_t g_image_proc_inited = 0;
static uint32_t g_next_lepton_retry_ms = 0;

static void Lepton_App_PreparePipeline(void)
{
    if (!g_image_proc_inited) {
        image_proc_init();
        g_image_proc_inited = 1;
    }
}

static uint8_t Lepton_App_TryInit(void)
{
    uint8_t rbfo_valid = 0;
    const lep_rbfo_t *rbfo;

    Lepton_App_PreparePipeline();
    g_pipeline_busy = 0;
    g_lepton_app_inited = 0;

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
