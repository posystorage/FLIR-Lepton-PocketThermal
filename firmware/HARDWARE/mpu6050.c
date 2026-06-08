#include "mpu6050.h"
#include "iic.h"
#include "debug.h"
#include "delay.h"

/* I2C interface */
#define MPU6050_I2C I2C1

/* Accel ±2g → 16384 LSB/g */
#define ACCEL_SCALE  16384L

/* LPF alpha (Q8): lpf = (lpf*(256-α) + raw*α) >> 8 */
#define ACC_ALPHA_NORMAL  32   /* ~0.125, ~4Hz cutoff @ 100Hz */
#define ACC_ALPHA_VIB     8    /* ~0.031, ~1Hz cutoff */

/* Gyro bias tracking LPF alpha */
#define GYRO_BIAS_ALPHA   4    /* ~0.016 */

/* Vibration energy threshold: sum(|gyro_hf|) for 3 axes (LSB) */
#define VIB_THRESHOLD      15000  /* ~115dps, only real shake freezes */

/* Orientation thresholds at ±2g (16384 LSB/g) */
#define ORIENT_TH_HIGH     4096  /* 0.25g: axis considered dominant */
#define ORIENT_TH_LOW      2458  /* 0.15g: below this both axes = flat */

/* Orientation update decimation: 100Hz read → 5Hz output */
#define ORIENT_DECIM       20

/* Confidence counter: consecutive same decisions */
#define ORIENT_CONFIDENCE  2

/* Raw readings */
static int16_t ax_raw, ay_raw, az_raw;
static int16_t gx_raw, gy_raw, gz_raw;

/* Low-pass filtered accelerometer (gravity vector) */
static int32_t ax_lpf, ay_lpf, az_lpf;

/* Gyro DC bias estimate */
static int32_t gx_bias, gy_bias, gz_bias;

/* Current orientation */
static volatile mpu6050_orientation_t g_orientation = MPU6050_ORIENTATION_UNKNOWN;

/* Service state */
static uint8_t g_mpu_ready = 0;
static uint8_t g_decim_cnt = 0;
static uint8_t g_conf_cnt = 0;
static mpu6050_orientation_t g_conf_candidate = MPU6050_ORIENTATION_UNKNOWN;

/* Tick flag: set by ISR, cleared by Service */
volatile uint8_t g_mpu_tick_flag = 0;

/* ── I2C helpers ── */
static __INLINE uint8_t mpu_write_reg(uint8_t reg, uint8_t val)
{
    return IIC_Write_Reg(MPU6050_I2C, MPU6050_ADDR, reg, val);
}

static __INLINE uint8_t mpu_read_burst(uint8_t reg, uint8_t *data, uint16_t len)
{
    return IIC_Read_Multi(MPU6050_I2C, MPU6050_ADDR, reg, data, len);
}

/* ── Initialization ── */
uint8_t MPU6050_Init(void)
{
    uint8_t whoami;
    uint8_t buf[14];
    uint8_t tmp;

    IIC1_Init();
		delay_ms(1);

    /* Probe: WHO_AM_I must return 0x68 */
    if(!IIC_Read_Reg(MPU6050_I2C, MPU6050_ADDR, MPU6050_WHO_AM_I, &whoami))
    {
        MPU_DEBUG("I2C probe failed");
        return 0;
    }
    if(whoami != 0x68)
    {
        MPU_DEBUG("WHO_AM_I=0x%02x (expect 0x68)", whoami);
        return 0;
    }
    MPU_DEBUG("detected");

    /* Wake up: clear SLEEP bit */
    if(!mpu_write_reg(MPU6050_PWR_MGMT_1, 0x00))
    {
        MPU_DEBUG("PWR_MGMT_1 write fail");
        return 0;
    }
    delay_ms(10);

    /* DLPF: accel 184Hz BW, gyro 188Hz BW, gyro rate=1kHz */
    if(!mpu_write_reg(MPU6050_CONFIG, 0x01))          { MPU_DEBUG("CONFIG write fail"); return 0; }

    /* Sample rate: 1kHz / (1 + SMPLRT_DIV) = 100Hz */
    if(!mpu_write_reg(MPU6050_SMPLRT_DIV, 9))         { MPU_DEBUG("SMPLRT_DIV write fail"); return 0; }

    /* Gyro ±250°/s, 131 LSB/dps */
    if(!mpu_write_reg(MPU6050_GYRO_CONFIG, 0x00))     { MPU_DEBUG("GYRO_CFG write fail"); return 0; }

    /* Accel ±2g, 16384 LSB/g */
    if(!mpu_write_reg(MPU6050_ACCEL_CONFIG, 0x00))    { MPU_DEBUG("ACCEL_CFG write fail"); return 0; }
		//if(!mpu_write_reg(MPU6050_ACCEL_CONFIG, 0x18))    { MPU_DEBUG("ACCEL_CFG write fail"); return 0; }//16G

    /* Verify PWR_MGMT_1 */
    if(IIC_Read_Reg(MPU6050_I2C, MPU6050_ADDR, MPU6050_PWR_MGMT_1, &tmp))
    {
        MPU_DEBUG("PWR_MGMT_1=0x%02x", tmp);
    }

    /* Seed filter states */
    if(mpu_read_burst(MPU6050_ACCEL_XOUT_H, buf, 14))
    {
        ax_raw = (int16_t)((buf[0]  << 8) | buf[1]);
        ay_raw = (int16_t)((buf[2]  << 8) | buf[3]);
        az_raw = (int16_t)((buf[4]  << 8) | buf[5]);
        gx_raw = (int16_t)((buf[8]  << 8) | buf[9]);
        gy_raw = (int16_t)((buf[10] << 8) | buf[11]);
        gz_raw = (int16_t)((buf[12] << 8) | buf[13]);

        ax_lpf  = ax_raw;
        ay_lpf  = ay_raw;
        az_lpf  = az_raw;
        gx_bias = gx_raw;
        gy_bias = gy_raw;
        gz_bias = gz_raw;
    }
    else
    {
        MPU_DEBUG("seed burst read fail");
    }

    g_mpu_ready = 1;
    MPU_DEBUG("init ok");
    return 1;
}

/* ── Orientation decision from filtered gravity vector ── */
static void orientation_update(void)
{
    mpu6050_orientation_t candidate;
    int32_t ax_abs = (ax_lpf > 0) ? ax_lpf : -ax_lpf;
    int32_t ay_abs = (ay_lpf > 0) ? ay_lpf : -ay_lpf;

    /* Flat: no dominant horizontal axis */
    if(ax_abs < ORIENT_TH_LOW && ay_abs < ORIENT_TH_LOW)
    {
        candidate = MPU6050_ORIENTATION_FLAT;
    }
    else if(ax_abs > ay_abs)
    {
        /* Landscape: X-axis dominant */
        if(ax_lpf < -ORIENT_TH_HIGH)
            candidate = MPU6050_ORIENTATION_90;
        else if(ax_lpf > ORIENT_TH_HIGH)
            candidate = MPU6050_ORIENTATION_270;
        else
            return;
    }
    else
    {
        /* Portrait: Y-axis dominant */
        if(ay_lpf < -ORIENT_TH_HIGH)
            candidate = MPU6050_ORIENTATION_0;
        else if(ay_lpf > ORIENT_TH_HIGH)
            candidate = MPU6050_ORIENTATION_180;
        else
            return;
    }

    /* Confidence counter: require N consecutive same */
    if(candidate == g_conf_candidate)
    {
        if(++g_conf_cnt >= ORIENT_CONFIDENCE)
        {
            if(g_orientation != candidate)
            {
                g_orientation = candidate;
                MPU_DEBUG("orient=%d (ax=%ld ay=%ld)", candidate, ax_lpf, ay_lpf);
            }
        }
    }
    else
    {
        g_conf_candidate = candidate;
        g_conf_cnt = 0;
    }
}

/* ── Periodic service: read + filter + orientation ── */
void MPU6050_Service(void)
{
    uint8_t buf[14];
    int32_t vib_energy;
    uint8_t alpha;
    int32_t tmp;

    if(!g_mpu_ready) return;

    /* Step 1: burst read 14 bytes (accel x3 + temp + gyro x3) */
    if(!mpu_read_burst(MPU6050_ACCEL_XOUT_H, buf, 14))
		{
			MPU_DEBUG("Burst_Read_Fail");			
			return;
		}
        

    ax_raw = (int16_t)((buf[0]  << 8) | buf[1]);
    ay_raw = (int16_t)((buf[2]  << 8) | buf[3]);
    az_raw = (int16_t)((buf[4]  << 8) | buf[5]);
    gx_raw = (int16_t)((buf[8]  << 8) | buf[9]);
    gy_raw = (int16_t)((buf[10] << 8) | buf[11]);
    gz_raw = (int16_t)((buf[12] << 8) | buf[13]);

    /* Step 2: gyro DC bias tracking (slow LPF) */
    gx_bias += (((int32_t)gx_raw - gx_bias) * GYRO_BIAS_ALPHA + 128) >> 8;
    gy_bias += (((int32_t)gy_raw - gy_bias) * GYRO_BIAS_ALPHA + 128) >> 8;
    gz_bias += (((int32_t)gz_raw - gz_bias) * GYRO_BIAS_ALPHA + 128) >> 8;

    /* Step 3: vibration energy from gyro high-frequency component */
    tmp = (int32_t)gx_raw - gx_bias;
    vib_energy = (tmp > 0) ? tmp : -tmp;
    tmp = (int32_t)gy_raw - gy_bias;
    vib_energy += (tmp > 0) ? tmp : -tmp;
    tmp = (int32_t)gz_raw - gz_bias;
    vib_energy += (tmp > 0) ? tmp : -tmp;

    /* Step 4: adaptive accelerometer LPF */
    alpha = (vib_energy > VIB_THRESHOLD) ? ACC_ALPHA_VIB : ACC_ALPHA_NORMAL;

    ax_lpf += (((int32_t)ax_raw - ax_lpf) * (int32_t)alpha + 128) >> 8;
    ay_lpf += (((int32_t)ay_raw - ay_lpf) * (int32_t)alpha + 128) >> 8;
    az_lpf += (((int32_t)az_raw - az_lpf) * (int32_t)alpha + 128) >> 8;

    /* Step 5: orientation decision (decimated to 5Hz) */
    if(++g_decim_cnt < ORIENT_DECIM)
        return;
    g_decim_cnt = 0;

    /* Adaptive LPF still runs during vibration; confidence counter provides stability */
    orientation_update();
}

/* ── Get current screen orientation ── */
mpu6050_orientation_t MPU6050_GetOrientation(void)
{
    return g_orientation;
}
