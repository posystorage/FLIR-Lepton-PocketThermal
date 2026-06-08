/*
 * lepton.c — FLIR Lepton 2.x / 3.x VoSPI 驱动
 *
 * 功能概述:
 *   - CCI (I2C) 命令通道: lep_get / lep_set / lep_run
 *   - VoSPI (SPI) 帧接收: 中断驱动 VSYNC + 主循环 Capture_Service
 *   - RAW14 / RGB888 双路径，RAW14 支持 FNV-1a 帧去重
 *   - RBFO 辐射参数读取，Telemetry Footer 模式
 */

#include "lepton.h"
#include "lp3921.h"
#include "iic.h"
#include "delay.h"
#include "key.h"
#include "uart4.h"
#include "sys_tick.h"
#include "debug.h"

/* CCI 命令缓冲区 (lep_set/lep_get 共用) */
uint16_t Lepton_Data_Buff[16];
/* VSYNC 中断置 1, Capture_Service 消费后清 0 */
uint8_t Lepton_New_Frame = 0;

/* ============================================================
 * 全局状态
 * ============================================================ */
static lep_rbfo_t g_rbfo;           /* 当前使用的 RBFO 参数 */
static uint8_t    g_rbfo_valid = 0; /* 1 = RAD 读取成功, 0 = 回退默认值 */

/* IDD 默认 RBFO (doc 06 §6.2 fallback) */
static const lep_rbfo_t rbfo_fallback = {
    395653u,     /* R */
    1428000u,    /* B x1000 */
    1000u,       /* F x1000 */
    156000       /* O x1000 */
};

/* ============================================================
 * RAW14 frame buffer
 * ============================================================ */
#ifdef Lepton_RGB88
    uint8_t  Lepton_Frame_Buf[60][240]; /* RGB888 */
#else
    uint16_t Lepton_Frame_Buf[60][80];  /* RAW14 */
#endif
static volatile uint8_t g_frame_ready = 0;
static volatile uint8_t g_consumer_busy = 0;

/* Telemetry rows (Footer mode: 3 rows x 80 words) */
uint16_t Lepton_Telemetry_Buf[3][80];

/* Frame statistics (debug) */
static volatile uint32_t g_frame_total   = 0;
static volatile uint32_t g_crc_errors    = 0;
//static volatile uint32_t g_seq_errors    = 0;
//static volatile uint32_t g_discard_count = 0;
static volatile uint32_t g_busy_drops    = 0;

/* 帧去重模式选择:
 *   FULL_READ   — 完整读取所有 63 行后计算签名，签名变化才发布
 *   PROBE_LINE  — 先读前 2 行计算前缀签名，相同则跳过整帧 (默认)
 */
#define LEPTON_DEDUP_MODE_FULL_READ   1u
#define LEPTON_DEDUP_MODE_PROBE_LINE  2u

#ifndef LEPTON_DEDUP_MODE
//#define LEPTON_DEDUP_MODE LEPTON_DEDUP_MODE_FULL_READ
#define LEPTON_DEDUP_MODE LEPTON_DEDUP_MODE_PROBE_LINE
#endif

#if (LEPTON_DEDUP_MODE != LEPTON_DEDUP_MODE_FULL_READ) && \
    (LEPTON_DEDUP_MODE != LEPTON_DEDUP_MODE_PROBE_LINE)
#error "Invalid LEPTON_DEDUP_MODE"
#endif

#ifndef Lepton_RGB88
/* 双行暂存缓冲: SPI 读入 scratch → CRC 通过后提交到 Frame_Buf */
static uint16_t g_line_cache[2][80];
/* 签名是否已有一帧历史 (首帧不做去重) */
static uint8_t  g_signature_valid = 0;
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
static uint32_t g_last_full_signature = 0;
#endif
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
static uint32_t g_last_prefix_signature = 0;
#endif
#endif

/* Compat alias for legacy code */
#define Lepton_Vidoe_Buff  Lepton_Frame_Buf

/* 重置所有采集状态 (init/deinit 时调用) */
static void Lepton_ResetCaptureState(void)
{
    Lepton_New_Frame = 0;
    g_frame_ready = 0;
    g_consumer_busy = 0;
#ifndef Lepton_RGB88
    g_signature_valid = 0;
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
    g_last_full_signature = 0;
#endif
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
    g_last_prefix_signature = 0;
#endif
#endif
}



/* ============================================================
 * CCI 读多字辅助 (从 0x0008 读取 words 个 16-bit word)
 * 返回: 1=成功, 0=失败
 * ============================================================ */
static uint8_t CCI_Read_DAT(uint16_t *buf, uint16_t words)
{
    if (buf == 0 || words == 0)
        return 0;
    return IIC0_Lepton_Read_DAT(buf, words);
}

/* ============================================================
 * lep_run — 执行无参数命令 (doc 06 §3)
 * 返回: LEP_OK / LEP_ERR_I2C / LEP_ERR_TIMEOUT / LEP_ERR_CCI
 * ============================================================ */
int lep_run(uint16_t cmd)
{
    uint16_t status;
    uint32_t start = GetTick();

    /* 等待 BUSY 清零 (超时保护) */
    do {
        if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &status) == 0)
            return LEP_ERR_I2C;
        if ((int32_t)(GetTick() - start) >= LEP_BUSY_TIMEOUT_MS)
            return LEP_ERR_TIMEOUT;
    } while (status & 0x01);

    /* RUN: data length = 0 */
    IIC0_Lepton_Write_Reg(LEP_I2C_DATA_LENGTH_REG, 0);
    IIC0_Lepton_Write_Reg(LEP_I2C_COMMAND_REG, cmd);

    /* 等待 BUSY 清零 */
    start = GetTick();
    do {
        if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &status) == 0)
            return LEP_ERR_I2C;
        if ((int32_t)(GetTick() - start) >= LEP_BUSY_TIMEOUT_MS)
            return LEP_ERR_TIMEOUT;
    } while (status & 0x01);

    /* 检查 CCI 错误码 (status 高字节) */
    if (status & 0xFF00)
        return LEP_ERR_CCI;

    return LEP_OK;
}

/* ============================================================
 * lep_set — 写参数命令 (doc 06 §3)
 * 返回: LEP_OK / LEP_ERR_I2C / LEP_ERR_TIMEOUT / LEP_ERR_CCI
 * ============================================================ */
int lep_set(uint16_t cmd, const uint16_t *data, uint16_t words)
{
    uint16_t status;
    uint32_t start = GetTick();

    /* 等待 BUSY 清零 */
    do {
        if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &status) == 0)
            return LEP_ERR_I2C;
        if ((int32_t)(GetTick() - start) >= LEP_BUSY_TIMEOUT_MS)
            return LEP_ERR_TIMEOUT;
    } while (status & 0x01);

    /* 写入数据到 0x0008 */
    IIC0_Lepton_Write_DAT((uint16_t *)data, words);
    /* 写入 Data Length */
    IIC0_Lepton_Write_Reg(LEP_I2C_DATA_LENGTH_REG, words);
    /* 写入 Command (完整值，不隐式修改) */
    IIC0_Lepton_Write_Reg(LEP_I2C_COMMAND_REG, cmd);

    /* 等待 BUSY 清零 */
    start = GetTick();
    do {
        if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &status) == 0)
            return LEP_ERR_I2C;
        if ((int32_t)(GetTick() - start) >= LEP_BUSY_TIMEOUT_MS)
            return LEP_ERR_TIMEOUT;
    } while (status & 0x01);

    /* 检查 CCI 错误码 */
    if (status & 0xFF00)
        return LEP_ERR_CCI;

    return LEP_OK;
}

/* ============================================================
 * lep_get — 读返回数据命令 (doc 06 §3)
 * 返回: LEP_OK / LEP_ERR_I2C / LEP_ERR_TIMEOUT / LEP_ERR_CCI
 * ============================================================ */
int lep_get(uint16_t cmd, uint16_t *data, uint16_t words)
{
    uint16_t status;
    uint32_t start = GetTick();

    /* 等待 BUSY 清零 */
    do {
        if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &status) == 0)
            return LEP_ERR_I2C;
        if ((int32_t)(GetTick() - start) >= LEP_BUSY_TIMEOUT_MS)
            return LEP_ERR_TIMEOUT;
    } while (status & 0x01);

    /* 写入 Data Length (期望返回的 word 数) */
    IIC0_Lepton_Write_Reg(LEP_I2C_DATA_LENGTH_REG, words);
    /* 写入 Command */
    IIC0_Lepton_Write_Reg(LEP_I2C_COMMAND_REG, cmd);

    /* 等待 BUSY 清零 */
    start = GetTick();
    do {
        if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &status) == 0)
            return LEP_ERR_I2C;
        if ((int32_t)(GetTick() - start) >= LEP_BUSY_TIMEOUT_MS)
            return LEP_ERR_TIMEOUT;
    } while (status & 0x01);

    /* 检查 CCI 错误码 */
    if (status & 0xFF00)
        return LEP_ERR_CCI;

    /* 读取返回数据 */
    if (words > 0 && data != 0) {
        if (CCI_Read_DAT(data, words) == 0)
            return LEP_ERR_I2C;
    }

    return LEP_OK;
}

/* ============================================================
 * RBFO 辅助: 两个 16-bit word → 一个 32-bit (doc 06 §6.2)
 * ============================================================ */
static uint32_t join_u32(uint16_t hi, uint16_t lo)
{
    return ((uint32_t)hi << 16) | lo;
}

uint8_t Lspton_Wait_Power_UP(void);
void Lepton_VoSPI_Init(void);

#define Lepton_SPI_CS PA3

/*
 * Lepton_HW_Prepare — 上电硬件准备 (需在 Lepton_Init 之前调用)
 * 开启 LP3921 CAM 电源，等待 Lepton 模组上电稳定
 */
void Lepton_HW_Prepare(void)
{
	LP3921_ENABLE_CAM();
	delay_ms(1000);
}

/*
 * Lepton_Init — 初始化 Lepton CCI + VoSPI 通道
 * 流程:
 *   1. 等待 Lepton 启动完成 (BOOT bit)
 *   2. 禁用机内 AGC，配置 RAW14 输出格式
 *   3. 探测 RAD 参数 (RBFO)，失败则使用默认值
 *   4. 使能 Telemetry Footer 模式
 *   5. 配置 GPIO VSYNC，初始化 VoSPI SPI 接口
 *   6. 使能 PA0 下降沿中断用于帧同步
 * 返回: 1=成功, 0=失败
 */
uint8_t Lepton_Init(void)
{
	int ret;
	uint16_t rad_data[8];

	g_rbfo = rbfo_fallback;
	g_rbfo_valid = 0;
	Lepton_ResetCaptureState();

	IIC0_Init();
	if (!IIC_Test_Per(I2C0, Lepton_IIC_ADDR))
		goto Lepton_Err;

	Lepton_VoSPI_Init();
	/* 配置硬件 CRC: 不翻转数据位, 初始值 0x0000, 使能 */
	CLK->AHBCLK |= CLK_AHBCLK_CRCCKEN_Msk;
	CRC->CTL = (0 << CRC_CTL_DATREV_Pos) | CRC_CTL_CHKSINIT_Pos | CRC_CTL_CRCEN_Pos;
	CRC->SEED = 0x0000;

	/* Step 1: Wait for boot */
	if (Lspton_Wait_Power_UP() == 0) goto Lepton_Err;

#ifdef Lepton_RGB88
	/* RGB888 path (preserved) */
	Lepton_Data_Buff[0] = 1; Lepton_Data_Buff[1] = 0;
	ret = lep_set(LEP_CMD_AGC_ENABLE_SET, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;
	Lepton_Data_Buff[0] = 7;
	ret = lep_set(0x0304, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;
	Lepton_Data_Buff[0] = 3;
	ret = lep_set(0x4828, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;
#else

	/* Step 2: Disable internal AGC */
	Lepton_Data_Buff[0] = 0; Lepton_Data_Buff[1] = 0;
	ret = lep_set(LEP_CMD_AGC_DISABLE_SET, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;

	/* Step 3: Set RAW14 output (OEM) */
	Lepton_Data_Buff[0] = 7; Lepton_Data_Buff[1] = 0;
	ret = lep_set(LEP_CMD_OEM_VIDEO_FORMAT_SET, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;

	/* Step 4-6: RAD probe + RBFO read */
	g_rbfo_valid = 0;
	Lepton_Data_Buff[0] = 1; Lepton_Data_Buff[1] = 0;
	ret = lep_set(LEP_CMD_RAD_ENABLE_SET, Lepton_Data_Buff, 2);
	if (ret == LEP_OK) 
	{
		ret = lep_get(LEP_CMD_RAD_ENABLE_GET, rad_data, 2);
		if (ret != LEP_OK) goto Lepton_Err;
		if (rad_data[0] == 1) 
		{
			ret = lep_get(LEP_CMD_RAD_RBFO_GET, rad_data, 8);
			if (ret == LEP_OK) 
			{
				g_rbfo.R = join_u32(rad_data[1], rad_data[0]);
				g_rbfo.B = join_u32(rad_data[3], rad_data[2]);
				g_rbfo.F = join_u32(rad_data[5], rad_data[4]);
				g_rbfo.O = (int32_t)join_u32(rad_data[7], rad_data[6]);
				g_rbfo_valid = 1;
			}
		}
	}
	if (!g_rbfo_valid) g_rbfo = rbfo_fallback;

	/* Step 7-8: Telemetry Enable + Footer */
	Lepton_Data_Buff[0] = 1; Lepton_Data_Buff[1] = 0;
	ret = lep_set(LEP_CMD_SYS_TELEMETRY_EN_SET,  Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;
	ret = lep_set(LEP_CMD_SYS_TELEMETRY_LOC_SET, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;
	
#endif

	/* Step 9: GPIO VSYNC (OEM) */
	Lepton_Data_Buff[0] = 5; Lepton_Data_Buff[1] = 0;
	ret = lep_set(LEP_CMD_OEM_GPIO_VSYNC_SET, Lepton_Data_Buff, 2);
	if (ret != LEP_OK) goto Lepton_Err;

	/* Step 10: VoSPI resync (CS high >=185ms) */
	Lepton_SPI_CS = 1;
	delay_ms(200);

	/* Setup PA0 VSYNC falling-edge interrupt */
	PA->MODE    &= ~0x03;
	PA->INTTYPE &= ~(1ul << 0);
	PA->INTEN   = (PA->INTEN & ~(0x00010001ul << 0))
	            | ((GPIO_INT_FALLING & 0xFFFFFFUL) << 0);
	NVIC_EnableIRQ(GPA_IRQn);
	return 1;

Lepton_Err:
	Lepton_ResetCaptureState();
	return 0;
}
/*
 * Lepton_Set — 向 Lepton 写入数据 (旧版接口, Data_Buff 需提前填充)
 * 等待 BUSY 清零后写入数据并发送 SET 命令
 */
void Lepton_Set(uint16_t Lepton_CMD_ID, uint16_t Lepton_Data_Length)
{
	uint16_t dat;
	do {
		if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &dat) == 0) return;
	} while (dat & 0x01);  /* 等待 BUSY 清零 */

	IIC0_Lepton_Write_DAT(Lepton_Data_Buff, Lepton_Data_Length);
	IIC0_Lepton_Write_Reg(LEP_I2C_DATA_LENGTH_REG, Lepton_Data_Length);
	IIC0_Lepton_Write_Reg(LEP_I2C_COMMAND_REG, Lepton_CMD_ID | 0x01);  /* SET 命令 */

	do {
		if (IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG, &dat) == 0) return;
		if (dat & 0xff00) return;  /* CCI 错误则退出 */
	} while (dat & 0x01);  /* 等待 BUSY 清零 */
}

/*
 * Lspton_Wait_Power_UP — 等待 Lepton 启动完成
 * 轮询 STATUS 寄存器 BOOT bit，超时约 5 秒
 * 返回: 1=启动完成, 0=超时或 I2C 错误
 */
uint8_t Lspton_Wait_Power_UP(void)
{
	uint16_t time_out=0;
	uint16_t dat;
	while(1)
	{
		if(IIC0_Lepton_Read_Reg(LEP_I2C_STATUS_REG,&dat)==0)return 0;
		if(dat & LEP_I2C_STATUS_BOOT_BIT_MASK)return 1;
		time_out++;
		if(time_out>500)return 0;
	}
}

/*
 * Lepton_VoSPI_Init — 初始化 SPI0 为 VoSPI 接收模式
 * PA1=MISO, PA2=CLK, PA3=CS (软件控制)
 * 16-bit 宽度, CLKPOL=1, RXNEG, 19.2MHz
 */
void Lepton_VoSPI_Init(void)
{
		CLK->APBCLK0 |= CLK_APBCLK0_SPI0CKEN_Msk;
    /* 选择 PCLK1 作为 SPI0 时钟源 */
		CLK->CLKSEL2 &= ~CLK_CLKSEL2_SPI0SEL_Msk;
		CLK->CLKSEL2 |= 0x20 << CLK_CLKSEL2_SPI0SEL_Pos;
    CLK_SetModuleClock(SPI0_MODULE, CLK_CLKSEL2_SPI0SEL_PCLK1, MODULE_NoMsk);		
    SYS->GPA_MFPL = (SYS->GPA_MFPL & ~(SYS_GPA_MFPL_PA1MFP_Msk | SYS_GPA_MFPL_PA2MFP_Msk)) |
                    (SYS_GPA_MFPL_PA1MFP_SPI0_MISO | SYS_GPA_MFPL_PA2MFP_SPI0_CLK);		
    /* Enable SPI0 clock pin (PA2) schmitt trigger */
    PA->SMTEN |= GPIO_SMTEN_SMTEN2_Msk;
	
		/* Master 模式, MSB, 16-bit, CLKPOL=1, RXNEG, 使能 */
		SPI0->CTL = (16 << SPI_CTL_DWIDTH_Pos) | SPI_CTL_RXNEG_Msk | SPI_CTL_TXNEG_Msk | SPI_CTL_CLKPOL_Msk | SPI_CTL_SPIEN_Msk;
		/* 备用分频: CLKDIV=5→16MHz, CLKDIV=11→8MHz */
		SPI0->CLKDIV = 4;  /* 96MHz / 5 = 19.2MHz */
		//SPI0->SSCTL = SPI_SSCTL_AUTOSS_Msk;  /* 自动片选 (备用) */
		Lepton_SPI_CS = 1;
		PA->MODE = (PA->MODE & ~(0x3ul << (3 * 2))) | (1 << (3 * 2));  /* PA3 推挽输出 */
		//PH9 = 0;
		//GPIO_SetMode(PH, BIT9, GPIO_MODE_OUTPUT);  // PH9:debug 
}


/*
 * GPA_IRQHandler — PA0 VSYNC 下降沿中断
 * 仅置位 Lepton_New_Frame 标志，主循环 Capture_Service 消费
 */
void GPA_IRQHandler(void)
{
    if ((PA->INTSRC & BIT0) == BIT0)
    {
        PA->INTSRC = BIT0;
        Lepton_New_Frame = 1;
    }
    else
    {
        PA->INTSRC = PA->INTSRC;
    }
}

/* SPI_Wait_Dat16 — 阻塞等待 SPI0 RX FIFO 非空并返回 16-bit 数据 */
uint16_t SPI_Wait_Dat16(void)
{
	while(SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
	return SPI0->RX;
}

/*
 * Lepton_Packet_Read — 读取一个 VoSPI packet (RGB888 兼容路径)
 * 返回: 0..62=valid packet ID, 0xFE=filtered, 0xFF=CRC 错误或非视频包
 */
#define LEPTON_PKT_FILTERED       0xFEu
#define LEPTON_PKT_FRAME_ERROR    0xFFu
#define LEPTON_FRAME_PACKET_TOTAL 63u
#define LEPTON_FRAME_READ_LIMIT   63u

#ifndef Lepton_RGB88
/*
 * FNV-1a 帧签名 — 用于 RAW14 帧去重
 * 原理: Lepton 以 24fps 输出但有效信息仅 ~8fps，相同热图像会重复 3 次。
 * 通过计算帧数据的 FNV-1a 哈希签名，跳过签名未变化的重复帧。
 */

/* FNV-1a 初始值 */
static uint32_t Lepton_SignatureInit(void)
{
    return 2166136261u;
}

/* FNV-1a: 将一个 16-bit word (高低字节分别) 混入签名 */
static uint32_t Lepton_SignatureAddWord(uint32_t signature, uint16_t word)
{
    signature ^= (uint8_t)(word >> 8);
    signature *= 16777619u;
    signature ^= (uint8_t)(word & 0xFFu);
    signature *= 16777619u;
    return signature;
}

/* 将一行 VoSPI 数据 (packet_id + 80 words) 混入签名 */
static uint32_t Lepton_SignatureAddRow(uint32_t signature, uint8_t packet_id,
                                       const uint16_t *row)
{
    uint32_t i;

    signature = Lepton_SignatureAddWord(signature, packet_id);
    for (i = 0; i < 80u; i++) {
        signature = Lepton_SignatureAddWord(signature, row[i]);
    }
    return signature;
}

/* 拷贝一行 80 个 uint16_t (160 字节) */
static void Lepton_CopyRow(uint16_t *dst, const uint16_t *src)
{
    uint32_t i;

    for (i = 0; i < 80u; i++) {
        dst[i] = src[i];
    }
}
#endif

#ifdef Lepton_RGB88
uint8_t Lepton_Packet_Read(void)
{
    uint16_t Packet_ID, CRC_Dat, dat;
    uint8_t  is_video;
    uint32_t pixel;
    uint16_t *pRow = 0;

    /* 重置 CRC 引擎 (每次新 packet) */
		CRC->CTL |= CRC_CTL_CHKSINIT_Msk;
    SPI0->CTL |= SPI_CTL_RXONLY_Msk;

    /* 读 Packet ID (高 4-bit 为类型, 低 6-bit 为 ID) */
    while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
    Packet_ID = SPI0->RX;		
    CRC->DAT = (Packet_ID>>8)&0x0F;
    CRC->DAT = Packet_ID&0xFF;
    CRC->DAT = 0;  /* CRC seed 已处理 */
		CRC->DAT = 0;
//		CRC->DAT = __REVSH(Packet_ID&0x0FFF);
//		CRC->DAT = 0;

    /* 读 Packet CRC */
    while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
    CRC_Dat = SPI0->RX;

    /* 判断 packet 类型 */
    if ((Packet_ID & 0x0f00) == 0x0f00) {
        is_video = 0;  /* empty/discard packet, filter after reading payload */
    } else {
        is_video = 1;
        Packet_ID &= 0x003f;  /* 保留 0..63 */
    }

    /* 读像素数据 */

    for (pixel = 0; pixel < 240; pixel += 2) {
        while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
        dat = SPI0->RX;
        dat = __REV((uint32_t)dat) >> 16;
        CRC->DAT = dat;
        if (is_video && Packet_ID < 60) {
            *((uint16_t*)&Lepton_Frame_Buf[Packet_ID][pixel]) = dat;
        }
        if (pixel == 236) SPI0->CTL &= ~SPI_CTL_RXONLY_Msk;
    }
		////RAW14
//    if (is_video && Packet_ID < 60) {
//        pRow = &Lepton_Frame_Buf[Packet_ID][0];
//    } else if (is_video && Packet_ID >= 60 && Packet_ID <= 62) {
//        pRow = &Lepton_Telemetry_Buf[Packet_ID - 60][0];
//    }
//    for (pixel = 0; pixel < 80; pixel++) 
//		{
//        while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
//        dat = SPI0->RX;
//        //CRC->DAT = dat;
//			  CRC->DAT = (dat>>8)&0xFF;
//				CRC->DAT = dat&0xFF;
//				//CRC->DAT = __REVSH(dat);
//        if (pRow != 0) pRow[pixel] = dat;
//        if (pixel == 78) SPI0->CTL &= ~SPI_CTL_RXONLY_Msk;
//    }
    /* CRC 校验 */
    if ((CRC->CHECKSUM&0xFFFF) != CRC_Dat) {
        g_crc_errors++;
        return 0xFF;  /* CRC fail → treat as discard */
    }

		//if(Packet_ID == 0)printf("%X %X %X\n",Packet_ID,CRC_Dat,CRC->CHECKSUM&0xFFFF);
		//LEP_DEBUG("%X",Packet_ID);*
    if (!is_video) return LEPTON_PKT_FRAME_ERROR;
    return (uint8_t)Packet_ID;
}
#else
/*
 * Lepton_Packet_Read_Row — RAW14 路径单行读取
 * 读取一个 VoSPI packet 到 scratch，CRC 通过后可选拷贝到 dst 并更新签名
 * 返回: 0..62=packet ID, 0xFF=CRC 错误或非视频包
 */
static uint8_t Lepton_Packet_Read_Row(uint16_t *scratch, uint16_t *dst,
                                      uint32_t *signature)
{
    uint16_t packet_id_raw, packet_crc, dat;
    uint8_t packet_id;
    uint8_t is_video;
    uint32_t pixel;

    CRC->CTL |= CRC_CTL_CHKSINIT_Msk;
    SPI0->CTL |= SPI_CTL_RXONLY_Msk;

    while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
    packet_id_raw = SPI0->RX;
    CRC->DAT = (packet_id_raw >> 8) & 0x0Fu;
    CRC->DAT = packet_id_raw & 0xFFu;
    CRC->DAT = 0;
    CRC->DAT = 0;

    while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
    packet_crc = SPI0->RX;

    if ((packet_id_raw & 0x0F00u) == 0x0F00u) {
        is_video = 0;
        packet_id = 0;
    } else {
        is_video = 1;
        packet_id = (uint8_t)(packet_id_raw & 0x003Fu);
    }

    for (pixel = 0; pixel < 80u; pixel++) {
        while (SPI_GET_RX_FIFO_EMPTY_FLAG(SPI0) == 1);
        dat = SPI0->RX;
        CRC->DAT = (dat >> 8) & 0xFFu;
        CRC->DAT = dat & 0xFFu;
        scratch[pixel] = dat;
        if (pixel == 78u) SPI0->CTL &= ~SPI_CTL_RXONLY_Msk;
    }
		//if((packet_id_raw & 0x003Fu) == 0)printf("%X %X %X\n",packet_id_raw,packet_crc,CRC->CHECKSUM&0xFFFF);
		//if((packet_id_raw & 0x003Fu) == 62)printf("%X %X %X\n",packet_id_raw,packet_crc,CRC->CHECKSUM&0xFFFF);
    if ((CRC->CHECKSUM&0xFFFF) != packet_crc) {
        g_crc_errors++;
        return LEPTON_PKT_FRAME_ERROR;
    }

    if (!is_video) {
        return LEPTON_PKT_FRAME_ERROR;
    }

    if (dst != 0) {
        Lepton_CopyRow(dst, scratch);
    }

    if (signature != 0) {
        *signature = Lepton_SignatureAddRow(*signature, packet_id, scratch);
    }

    return packet_id;
}
#endif

/* ============================================================
 * Lepton_Capture_Service — 每帧调用, VSYNC 后读取全部 packets
 * Footer 模式: 60 图像 packet (0..59) + 3 telemetry packet (60..62)
 * ============================================================ */
uint8_t Lepton_Capture_Service(void)
{
#ifdef Lepton_RGB88
    uint32_t pkt_count;
    int32_t  expected_id;
    uint8_t  pkt_id;

    if (!Lepton_New_Frame)
        return 0;
    Lepton_New_Frame = 0;
    if (g_consumer_busy) {
        g_busy_drops++;
        return 0;
    }

    /* 开始读取: CS 拉低 */
    Lepton_SPI_CS = 0;
    delay_us(20);

    expected_id = 0;
    for (pkt_count = 0; pkt_count < LEPTON_FRAME_READ_LIMIT; pkt_count++)
    {
        pkt_id = Lepton_Packet_Read();
        if (pkt_id == LEPTON_PKT_FRAME_ERROR || pkt_id >= LEPTON_FRAME_PACKET_TOTAL) {
            goto Frame_Discard;
        }
        if (pkt_id != expected_id) {
            goto Frame_Discard;
        }
        expected_id = pkt_id + 1;
        /* 所有 63 个 packet 收完 (0..62) */
        if (expected_id >= LEPTON_FRAME_PACKET_TOTAL)
            break;
    }

    /* 帧完成 */
    if (expected_id < LEPTON_FRAME_PACKET_TOTAL) {
        goto Frame_Discard;
    }

    delay_us(10);
    Lepton_SPI_CS = 1;

    /* 标记当前 buf 为 ready, 切换写入 buf */
    g_frame_ready = 1;
    g_frame_total++;
    return 1;

Frame_Discard:
    delay_us(10);
    Lepton_SPI_CS = 1;
    /* CS 重同步: 拉高 >=185ms, 等待新 VSYNC */
    delay_ms(2);
    return 0;
#else
    uint8_t pkt_id;
    uint8_t expected_id;
    uint8_t start_id;
    uint16_t *scratch;
    uint16_t *dst;
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
    uint32_t full_signature;
#endif
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
    uint32_t prefix_signature;
#endif

    if (!Lepton_New_Frame)
        return 0;
    Lepton_New_Frame = 0;
    if (g_consumer_busy) {
        g_busy_drops++;
        return 0;
    }
		//printf("1");
    g_frame_ready = 0;

		__disable_irq();
    Lepton_SPI_CS = 0;
    delay_us(20);

#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
    full_signature = Lepton_SignatureInit();
#endif
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
    prefix_signature = Lepton_SignatureInit();
#endif
    start_id = 0;

#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
    if (g_signature_valid) {
        pkt_id = Lepton_Packet_Read_Row(g_line_cache[0], 0, &prefix_signature);
        if (pkt_id != 0u) {
            goto Frame_Discard;
        }

        pkt_id = Lepton_Packet_Read_Row(g_line_cache[1], 0, &prefix_signature);
        if (pkt_id != 1u) {
            goto Frame_Discard;
        }

        if (prefix_signature == g_last_prefix_signature) {
            delay_us(10);
            Lepton_SPI_CS = 1;
						__enable_irq();
            return 0;
        }

        Lepton_CopyRow(&Lepton_Frame_Buf[0][0], g_line_cache[0]);
        Lepton_CopyRow(&Lepton_Frame_Buf[1][0], g_line_cache[1]);
        start_id = 2u;
    }
#endif

    for (expected_id = start_id; expected_id < LEPTON_FRAME_PACKET_TOTAL; expected_id++) 
		{
        scratch = g_line_cache[expected_id & 1u];
        if (expected_id < 60u) {
            dst = &Lepton_Frame_Buf[expected_id][0];
        } else {
            dst = &Lepton_Telemetry_Buf[expected_id - 60u][0];
        }

#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
        pkt_id = Lepton_Packet_Read_Row(scratch, dst, &full_signature);
#else
        pkt_id = Lepton_Packet_Read_Row(scratch, dst, 0);
#endif
        if (pkt_id != expected_id) {
            goto Frame_Discard;
        }

#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
        if (expected_id == 0u || expected_id == 1u) {
            prefix_signature = Lepton_SignatureAddRow(prefix_signature, expected_id, scratch);
        }
#endif
    }

#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
    if (g_signature_valid && full_signature == g_last_full_signature) {
        delay_us(10);
        Lepton_SPI_CS = 1;
				__enable_irq();
        return 0;
    }
#endif

    delay_us(10);
    Lepton_SPI_CS = 1;
		__enable_irq();

#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_FULL_READ)
    g_last_full_signature = full_signature;
#endif
#if (LEPTON_DEDUP_MODE == LEPTON_DEDUP_MODE_PROBE_LINE)
    g_last_prefix_signature = prefix_signature;
#endif
    g_signature_valid = 1;
    g_frame_ready = 1;
    g_frame_total++;
		//printf("A");
    return 1;

Frame_Discard:
    delay_us(10);
    Lepton_SPI_CS = 1;
		__enable_irq();
    delay_ms(2);
    return 0;
#endif
}

/*
 * Lepton_GetReadyFrame — 获取已就绪帧指针
 * 返回: RAW14 帧指针 [60][80], 无就绪帧则返回 0
 */
const uint16_t (*Lepton_GetReadyFrame(void))[80]
{
#ifdef Lepton_RGB88
    return 0;
#else
    if (!g_frame_ready)
        return 0;
    return (const uint16_t (*)[80])Lepton_Frame_Buf;
#endif
}

/* Lepton_GetRBFO — 获取当前 RBFO 辐射参数, valid=1 表示来自 RAD 读取 */
const lep_rbfo_t *Lepton_GetRBFO(uint8_t *valid)
{
    if (valid != 0)
        *valid = g_rbfo_valid;
    return &g_rbfo;
}

/* 释放已就绪帧 (消费端处理完成后调用) */
void Lepton_ReleaseReadyFrame(void)
{
    g_frame_ready = 0;
}

/* 通知驱动消费端是否忙碌 (忙碌时跳过帧采集避免覆盖) */
void Lepton_SetConsumerBusy(uint8_t busy)
{
    g_consumer_busy = busy ? 1u : 0u;
}

/* 获取因消费端忙碌而丢弃的帧计数 */
uint32_t Lepton_GetBusyDropCount(void)
{
    return g_busy_drops;
}

/*
 * Lepton_Deinit — 关闭 Lepton 驱动
 * 禁用 SPI0 时钟、PA0 VSYNC 中断，重置采集状态
 */
void Lepton_Deinit(void)
{
    /* 禁用 SPI0 */
    SPI0->CTL &= ~SPI_CTL_SPIEN_Msk;
    CLK->APBCLK0 &= ~CLK_APBCLK0_SPI0CKEN_Msk;

    /* 禁用 PA0 帧同步中断 */
    NVIC_DisableIRQ(GPA_IRQn);
    PA->INTEN &= ~BIT0;
    PA->INTSRC = BIT0;
    Lepton_ResetCaptureState();
}

