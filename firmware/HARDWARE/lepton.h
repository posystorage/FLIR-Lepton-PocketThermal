#ifndef _LEPTON_H_
#define _LEPTON_H_

#include "M480.h"

/* Define Lepton_RGB88 for legacy RGB888 output. Default path is RAW14. */
//#define Lepton_RGB88

#define LEP_OK            0
#define LEP_ERR_I2C       (-1)
#define LEP_ERR_TIMEOUT   (-2)
#define LEP_ERR_CCI       (-3)

#define LEP_BUSY_TIMEOUT_MS   100

#define LEP_OEM_PROTECTION_BIT  0x4000u

#define LEP_OP_GET    0u
#define LEP_OP_SET    1u
#define LEP_OP_RUN    2u

/* Command words written directly to 0x0004. */
#define LEP_CMD_AGC_DISABLE_SET       0x0101u
#define LEP_CMD_AGC_ENABLE_SET        0x0101u

#define LEP_CMD_SYS_FPA_TEMP_GET      0x0214u
#define LEP_CMD_SYS_TELEMETRY_EN_SET  0x0219u
#define LEP_CMD_SYS_TELEMETRY_LOC_SET 0x021Du
#define LEP_CMD_SYS_FFC_RUN           0x0242u
#define LEP_CMD_SYS_FFC_STATUS_GET    0x0244u
#define LEP_CMD_SYS_FFC_STATE_GET     0x024Cu

#define LEP_CMD_OEM_VIDEO_FORMAT_SET  0x4829u
#define LEP_CMD_OEM_GPIO_VSYNC_SET    0x4855u

#define LEP_CMD_RAD_RBFO_GET          0x4E04u
#define LEP_CMD_RAD_ENABLE_GET        0x4E10u
#define LEP_CMD_RAD_ENABLE_SET        0x4E11u

#define LEP_I2C_DEVICE_ADDRESS        0x2A
#define LEP_I2C_STATUS_REG            0x0002u
#define LEP_I2C_COMMAND_REG           0x0004u
#define LEP_I2C_DATA_LENGTH_REG       0x0006u
#define LEP_I2C_DATA_0_REG            0x0008u

#define LEP_I2C_STATUS_BUSY_BIT_MASK  0x0001u
#define LEP_I2C_STATUS_BOOT_BIT_MASK  0x0004u

#define Lepton_IIC_ADDR               (LEP_I2C_DEVICE_ADDRESS << 1)

/* RBFO 辐射校准参数 (Radiometric Calibration Data) */
typedef struct {
    uint32_t R;  /* 辐射响应率 */
    uint32_t B;  /* B x1000 */
    uint32_t F;  /* F x1000 */
    int32_t  O;  /* O x1000 */
} lep_rbfo_t;

/* Telemetry Footer 数据 (3 行 x 80 words, 帧尾附加) */
typedef struct {
    uint16_t telemetry_rows[3][80];
} lep_telemetry_t;

/* CCI 命令接口 */
int  lep_get(uint16_t cmd, uint16_t *data, uint16_t words);
int  lep_set(uint16_t cmd, const uint16_t *data, uint16_t words);
int  lep_run(uint16_t cmd);

/* 生命周期 */
uint8_t Lepton_Init(void);
void Lepton_HW_Prepare(void);
void Lepton_Deinit(void);

/* 帧采集接口 */
void Lepton_Set(uint16_t Lepton_CMD_ID, uint16_t Lepton_Data_Length);
uint8_t Lepton_Capture_Service(void);
const uint16_t (*Lepton_GetReadyFrame(void))[80];
const lep_rbfo_t *Lepton_GetRBFO(uint8_t *valid);
void Lepton_ReleaseReadyFrame(void);
void Lepton_SetConsumerBusy(uint8_t busy);
uint32_t Lepton_GetBusyDropCount(void);

#endif
