#ifndef _DEBUG_H
#define _DEBUG_H
#include <stdio.h>

/* ── 调试总开关 ── */
#define DEBUG_MASTER_ENABLE     1   /* 1=启用全部调试输出, 0=关闭全部 */

/* ── 各模块调试开关 (仅在总开关启用时生效) ── */
#define DEBUG_PM_ENABLE         0   /* 电源管理 */
#define DEBUG_HW_ENABLE         1   /* 硬件 (按键/LCD/I2C/UART/GPIO等, 不含Lepton/SD/USB) */
#define DEBUG_LEPTON_ENABLE     1   /* Lepton 热成像摄像头 */
#define DEBUG_SD_ENABLE         0   /* SD卡 */
#define DEBUG_USB_ENABLE        0   /* USB */
#define DEBUG_MSC_ENABLE        0   /* USB MSC */
#define DEBUG_IMG_ENABLE        1   /* 图像处理算法 */
#define DEBUG_MPU_ENABLE        0   /* MPU6050 六轴姿态 */

/* ── 模块调试宏 ── */

/* 电源管理: PM_DEBUG("fmt", ...) → "PM: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_PM_ENABLE)
#define PM_DEBUG(fmt, ...)      printf("PM: " fmt "\r\n", ##__VA_ARGS__)
#else
#define PM_DEBUG(fmt, ...)      ((void)0)
#endif

/* 硬件: HW_DEBUG("fmt", ...) → "HW: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_HW_ENABLE)
#define HW_DEBUG(fmt, ...)      printf("HW: " fmt "\r\n", ##__VA_ARGS__)
#else
#define HW_DEBUG(fmt, ...)      ((void)0)
#endif

/* Lepton: LEP_DEBUG("fmt", ...) → "LEP: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_LEPTON_ENABLE)
#define LEP_DEBUG(fmt, ...)     printf("LEP: " fmt "\r\n", ##__VA_ARGS__)
#else
#define LEP_DEBUG(fmt, ...)     ((void)0)
#endif

/* SD卡: SD_DEBUG("fmt", ...) → "SD: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_SD_ENABLE)
#define SD_DEBUG(fmt, ...)      printf("SD: " fmt "\r\n", ##__VA_ARGS__)
#else
#define SD_DEBUG(fmt, ...)      ((void)0)
#endif

/* USB: USB_DEBUG("fmt", ...) → "USB: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_USB_ENABLE)
#define USB_DEBUG(fmt, ...)     printf("USB: " fmt "\r\n", ##__VA_ARGS__)
#else
#define USB_DEBUG(fmt, ...)     ((void)0)
#endif

/* MSC: MSC_DEBUG("fmt", ...) → "MSC: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_MSC_ENABLE)
#define MSC_DEBUG(fmt, ...)     printf("MSC: " fmt "\r\n", ##__VA_ARGS__)
#else
#define MSC_DEBUG(fmt, ...)     ((void)0)
#endif


/* 图像处理: IMG_DEBUG("fmt", ...) → "IMG: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_IMG_ENABLE)
#define IMG_DEBUG(fmt, ...)     printf("IMG: " fmt "\r\n", ##__VA_ARGS__)
#else
#define IMG_DEBUG(fmt, ...)     ((void)0)
#endif

/* MPU6050: MPU_DEBUG("fmt", ...) → "MPU: fmt\r\n" */
#if (DEBUG_MASTER_ENABLE && DEBUG_MPU_ENABLE)
#define MPU_DEBUG(fmt, ...)     printf("MPU: " fmt "\r\n", ##__VA_ARGS__)
#else
#define MPU_DEBUG(fmt, ...)     ((void)0)
#endif

#endif
