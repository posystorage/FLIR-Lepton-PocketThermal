#ifndef _IMAGE_UPSCALE_H_
#define _IMAGE_UPSCALE_H_

#include <stdint.h>

/*
 * 显示灰度扩展模式：
 * - NEAREST_X4: 保留旧模式常量，当前顶层 image_pipeline_run 已不再支持 X4 直出。
 * - SR_2X2_LIMITED: AGC 后 Gray8 先扩展为 320x240，再查色卡输出。
 */
#define IMAGE_UPSCALE_NEAREST_X4        0u
#define IMAGE_UPSCALE_SR_2X2_LIMITED    1u

#ifndef IMAGE_UPSCALE_MODE
#define IMAGE_UPSCALE_MODE              IMAGE_UPSCALE_SR_2X2_LIMITED
#endif

#define IMAGE_UPSCALE_SRC_W             80u
#define IMAGE_UPSCALE_SRC_H             60u
#define IMAGE_UPSCALE_MID_W             (IMAGE_UPSCALE_SRC_W * 2u)
#define IMAGE_UPSCALE_MID_H             (IMAGE_UPSCALE_SRC_H * 2u)
#define IMAGE_UPSCALE_OUT_W             (IMAGE_UPSCALE_MID_W * 2u)
#define IMAGE_UPSCALE_OUT_H             (IMAGE_UPSCALE_MID_H * 2u)

/*
 * 锐化模式：
 * OFF         : 只做扩展，不做最终锐化。
 * LIGHT       : 使用 3 行缓存原地轻锐化，RAM 代价低，当前默认。
 * LIGHT_FRAME : 使用整帧锐化输出缓存，主要保留给调试/对比。
 */
#define IMAGE_UPSCALE_SHARPEN_OFF       0u
#define IMAGE_UPSCALE_SHARPEN_LIGHT     1u
#define IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME 2u

#ifndef IMAGE_UPSCALE_SHARPEN_MODE
#define IMAGE_UPSCALE_SHARPEN_MODE      IMAGE_UPSCALE_SHARPEN_LIGHT
#endif

#if (IMAGE_UPSCALE_MODE != IMAGE_UPSCALE_NEAREST_X4) && \
    (IMAGE_UPSCALE_MODE != IMAGE_UPSCALE_SR_2X2_LIMITED)
#error "Invalid IMAGE_UPSCALE_MODE"
#endif

#if (IMAGE_UPSCALE_SHARPEN_MODE != IMAGE_UPSCALE_SHARPEN_OFF) && \
    (IMAGE_UPSCALE_SHARPEN_MODE != IMAGE_UPSCALE_SHARPEN_LIGHT) && \
    (IMAGE_UPSCALE_SHARPEN_MODE != IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME)
#error "Invalid IMAGE_UPSCALE_SHARPEN_MODE"
#endif

#if (IMAGE_UPSCALE_MODE == IMAGE_UPSCALE_SR_2X2_LIMITED)
/* 启动分片式超分状态机，源数据必须在处理完成前保持有效。 */
void image_upscale_start(uint8_t src[IMAGE_UPSCALE_SRC_H][IMAGE_UPSCALE_SRC_W]);

/* 推进最多 row_budget 行；返回 1 表示 320x240 结果已可读取。 */
uint8_t image_upscale_step(uint32_t row_budget);

/* 获取最近一次超分输出。LIGHT 模式下返回 g_sr_out 原地锐化后的结果。 */
uint8_t (*image_upscale_get_result(void))[IMAGE_UPSCALE_OUT_W];
#if (IMAGE_UPSCALE_SHARPEN_MODE != IMAGE_UPSCALE_SHARPEN_OFF)
/* 单点轻锐化调试接口，LIGHT_FRAME 模式或算法验证时使用。 */
uint8_t image_upscale_sharpen_light_at(uint8_t src[IMAGE_UPSCALE_OUT_H][IMAGE_UPSCALE_OUT_W],
                                       uint32_t x, uint32_t y);
#endif

/* 同步兼容接口：内部启动状态机并一次性跑完整帧。 */
uint8_t (*image_upscale_sr320(uint8_t src[IMAGE_UPSCALE_SRC_H][IMAGE_UPSCALE_SRC_W]))
	[IMAGE_UPSCALE_OUT_W];
#endif

#endif
