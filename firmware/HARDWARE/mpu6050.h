#ifndef _MPU6050_H_
#define _MPU6050_H_
#include "M480.h"

/* I2C address (AD0=GND) */
#define MPU6050_ADDR_7BIT   0x68
#define MPU6050_ADDR        (0x68 << 1)  /* 0xD0, write address */

/* Register map */
#define MPU6050_SMPLRT_DIV  0x19
#define MPU6050_CONFIG      0x1A
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_ACCEL_XOUT_L 0x3C
#define MPU6050_ACCEL_YOUT_H 0x3D
#define MPU6050_ACCEL_YOUT_L 0x3E
#define MPU6050_ACCEL_ZOUT_H 0x3F
#define MPU6050_ACCEL_ZOUT_L 0x40
#define MPU6050_TEMP_OUT_H  0x41
#define MPU6050_TEMP_OUT_L  0x42
#define MPU6050_GYRO_XOUT_H 0x43
#define MPU6050_GYRO_XOUT_L 0x44
#define MPU6050_GYRO_YOUT_H 0x45
#define MPU6050_GYRO_YOUT_L 0x46
#define MPU6050_GYRO_ZOUT_H 0x47
#define MPU6050_GYRO_ZOUT_L 0x48
#define MPU6050_PWR_MGMT_1  0x6B
#define MPU6050_WHO_AM_I    0x75

typedef enum {
    MPU6050_ORIENTATION_0 = 0,      /* Portrait normal: Ay < -threshold  */
    MPU6050_ORIENTATION_180,        /* Portrait upside-down: Ay > threshold */
    MPU6050_ORIENTATION_90,         /* Landscape (right): Ax < -threshold */
    MPU6050_ORIENTATION_270,        /* Landscape (left):  Ax >  threshold */
    MPU6050_ORIENTATION_FLAT,       /* Device nearly flat, no dominant axis */
    MPU6050_ORIENTATION_UNKNOWN     /* Not yet determined */
} mpu6050_orientation_t;

uint8_t MPU6050_Init(void);
void    MPU6050_Service(void);
mpu6050_orientation_t MPU6050_GetOrientation(void);

extern volatile uint8_t g_mpu_tick_flag;

#endif
