#include "stdio.h"
#include "M480.h"
#include "delay.h"
#include "system_init.h"
#include "sys_tick.h"
#include "lp3921.h"
#include "lcd.h"
#include "lepton.h"
#include "lepton_app.h"
#include "key.h"
#include "uart4.h"
#include "debug.h"
#include "sdcard.h"
#include "power_manager.h"
#include "mpu6050.h"
#include "usb_composite.h"
#include "storage.h"
#include "ui.h"
#include "ff.h"
#include "diskio.h"

#define ENABLE_FATFS_TEST  0

uint8_t test_run = 0;

#if ENABLE_FATFS_TEST
static void FATFS_Test(void)
{
    FIL fil;
    FRESULT res;
    UINT bw, br;
    char buf[64];
    DIR dir;
    FILINFO fno;

    if (test_run) return;
    test_run = 1;

    res = f_open(&fil, "0:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        f_write(&fil, "Hello from FLIR LWIR!\r\n", 23, &bw);
        f_close(&fil);
        SD_DEBUG("write test.txt ok, bytes=%u", (unsigned int)bw);
    } else {
        SD_DEBUG("f_open write err: %d", (int)res);
    }

    res = f_open(&fil, "0:test.txt", FA_READ);
    if (res == FR_OK) {
        f_read(&fil, buf, sizeof(buf) - 1, &br);
        buf[br] = 0;
        f_close(&fil);
        SD_DEBUG("read test.txt: '%s'", buf);
    }

    res = f_opendir(&dir, "0:");
    if (res == FR_OK) {
        SD_DEBUG("root dir:");
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            SD_DEBUG("  %s%s  %lu", fno.fname,
                     (fno.fattrib & AM_DIR) ? "/" : "",
                     (unsigned long)fno.fsize);
        }
        f_closedir(&dir);
    }
    SD_DEBUG("FS test done");
}
#endif

void Debug_GPIO_Init(void)
{
    PA14 = 1;
    PA15 = 1;
    PA->MODE &= ~0xF0000000;
    PA->MODE |= 0x50000000;
}

int32_t main(void)
{
    delay_init();
    Timer0_Init();
    Uart4_Init();
    Power_Init();
    Key_Init();
    Debug_GPIO_Init();

    LP3921_ENABLE_PER();
    SDCard_Init();
    LCD_Init();
    MPU6050_Init();
    Lepton_HW_Prepare();
    USB_Composite_Init();
    Lepton_App_Init();
    Storage_Init();
    UI_Init();

    HW_DEBUG("boot ok\r\n");

    while (1)
    {
        key_event_t ev;

        if (!Storage_IsBusy()) {
            Lepton_Frame_Service();
        }

        while (Key_GetEvent(&ev))
        {
            Power_OnKeyEvent(ev.key_id, ev.event);
            UI_OnKeyEvent(&ev);
        }

        Power_Service();
        SDCard_Service();
        Storage_Service();
        UI_Service();
#if ENABLE_FATFS_TEST
        FATFS_Test();
#endif
        USB_Composite_Service();
    }
}
