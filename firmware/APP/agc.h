#ifndef _AGC_H_
#define _AGC_H_

#include <stdint.h>

/*
 * AGC 输入为 Lepton RAW14[60][80]，输出为显示用 Gray8[60][80]。
 * 当前默认使用 512-bin 直方图均衡变体：RAW14 右移 5 bit 后落入 bin，
 * 既降低直方图 RAM/运算量，也保留足够的热层次分辨率。
 */
#define AGC_BINS        512u
#define AGC_SHIFT       5u
#define AGC_CLIP_HIGH   96u
#define AGC_CLIP_LOW    4u
#define AGC_DAMP_NEW    64u   /* 64/256 new + 192/256 old */
#define AGC_PIXELS      (60u * 80u)

#define AGC_MODE_ROBUST_MINMAX  1u
#define AGC_MODE_HEQ            2u

/* Backward-compatible name for the first debug version. */
#define AGC_MODE_LINEAR         AGC_MODE_ROBUST_MINMAX

#ifndef AGC_MODE
#define AGC_MODE         AGC_MODE_HEQ
#endif

/* 整帧几乎无动态范围时输出中灰，避免显示端出现纯黑或纯白闪烁。 */
#define AGC_FLAT_OUTPUT  128u

/* Robust min/max 模式仅忽略少量冷端离群点，热端默认不裁掉，保留小热点。 */
#define AGC_ROBUST_LOW_CUT_PIXELS   8u
#define AGC_ROBUST_HIGH_CUT_PIXELS  0u

/* 初始化 AGC 内部历史映射。切换 AGC 模式或重新开始显示时调用。 */
void    agc_init(void);

/* 根据当前 RAW14 帧构建灰度映射表。HEQ 模式下会更新 map8/prev_map8。 */
void    agc_build_map(const uint16_t raw14[60][80]);

/* 使用最近一次 agc_build_map() 得到的映射表，把 RAW14 渲染为 Gray8。 */
void    agc_render(const uint16_t raw14[60][80], uint8_t display8[60][80]);

#endif
