#include "agc.h"
#include "debug.h"
#include <string.h>

#if (AGC_MODE != AGC_MODE_ROBUST_MINMAX) && (AGC_MODE != AGC_MODE_HEQ)
#error "Invalid AGC_MODE"
#endif

/* 512-bin 直方图复用为工作缓冲。HEQ 模式中，裁剪和重分配会原地修改 hist。 */
static uint16_t hist[AGC_BINS];

#if (AGC_MODE == AGC_MODE_ROBUST_MINMAX)
/* Robust min/max 模式的线性映射参数。 */
static uint16_t g_raw_min;
static uint16_t g_raw_range;
#endif

#if (AGC_MODE == AGC_MODE_HEQ)
/*
 * map8      : 当前帧 RAW14 bin -> Gray8 映射。
 * prev_map8 : 上一帧映射，用于帧间阻尼，降低自动增益跳变带来的闪烁。
 * map_valid : 第一帧没有历史映射，不能做阻尼混合。
 */
static uint8_t map8[AGC_BINS];
static uint8_t prev_map8[AGC_BINS];
static uint8_t map_valid;
#endif

/* Pass 1: 扫描 80x60 RAW14，压缩到 512-bin 直方图。 */
static void agc_build_hist(const uint16_t raw14[60][80])
{
	uint32_t y, x;

	memset(hist, 0, sizeof(hist));
	for (y = 0; y < 60; y++) {
		for (x = 0; x < 80; x++) {
			++hist[(raw14[y][x] & 0x3FFFu) >> AGC_SHIFT];
		}
	}
}

/* 找到实际有像素的首尾 bin，后续 HEQ 只处理有效动态范围，减少背景空 bin 干扰。 */
static uint8_t agc_find_active_bins(uint16_t *first_bin, uint16_t *last_bin)
{
	uint32_t i;

	for (i = 0; i < AGC_BINS; i++) {
		if (hist[i] != 0u) {
			*first_bin = (uint16_t)i;
			break;
		}
	}
	if (i >= AGC_BINS) {
		*first_bin = 0;
		*last_bin = 0;
		return 0;
	}

	for (i = AGC_BINS; i > 0; i--) {
		if (hist[i - 1u] != 0u) {
			*last_bin = (uint16_t)(i - 1u);
			return 1;
		}
	}

	*last_bin = *first_bin;
	return 1;
}

#if (AGC_MODE == AGC_MODE_ROBUST_MINMAX)
/* bin 边界换回 RAW14 近似值，供 robust min/max 线性模式使用。 */
static uint16_t agc_bin_raw_min(uint16_t bin)
{
	return (uint16_t)((uint32_t)bin << AGC_SHIFT);
}

static uint16_t agc_bin_raw_max(uint16_t bin)
{
	uint32_t raw_max = (((uint32_t)bin + 1u) << AGC_SHIFT) - 1u;

	if (raw_max > 0x3FFFu) raw_max = 0x3FFFu;
	return (uint16_t)raw_max;
}


static uint16_t agc_find_low_cut_bin(uint32_t skip_pixels)
{
	uint32_t i, sum = 0;

	for (i = 0; i < AGC_BINS; i++) {
		sum += hist[i];
		if (sum > skip_pixels) {
			return (uint16_t)i;
		}
	}

	return (uint16_t)(AGC_BINS - 1u);
}

static uint16_t agc_find_high_cut_bin(uint32_t skip_pixels)
{
	uint32_t i, sum = 0;

	for (i = AGC_BINS; i > 0; i--) {
		sum += hist[i - 1u];
		if (sum > skip_pixels) {
			return (uint16_t)(i - 1u);
		}
	}

	return 0;
}
#endif

void agc_init(void)
{
	/* 初始化所有状态，保证第一帧不会混入旧映射。 */
	memset(hist, 0, sizeof(hist));

#if (AGC_MODE == AGC_MODE_ROBUST_MINMAX)
	g_raw_min = 0;
	g_raw_range = 0;
#elif (AGC_MODE == AGC_MODE_HEQ)
	memset(map8, 0, sizeof(map8));
	memset(prev_map8, 0, sizeof(prev_map8));
	map_valid = 0;
#endif
}

void agc_build_map(const uint16_t raw14[60][80])
{
	uint16_t first_bin, last_bin;

	/* 先建立直方图，再判断是否存在有效动态范围。 */
	agc_build_hist(raw14);
	if (!agc_find_active_bins(&first_bin, &last_bin) || first_bin == last_bin) {
#if (AGC_MODE == AGC_MODE_ROBUST_MINMAX)
		g_raw_min = agc_bin_raw_min(first_bin);
		g_raw_range = 0;
#elif (AGC_MODE == AGC_MODE_HEQ)
		memset(map8, AGC_FLAT_OUTPUT, sizeof(map8));
		memset(prev_map8, AGC_FLAT_OUTPUT, sizeof(prev_map8));
		map_valid = 1;
#endif
		return;
	}

#if (AGC_MODE == AGC_MODE_ROBUST_MINMAX)
	{
		uint16_t low_bin;
		uint16_t high_bin;
		uint16_t raw_min;
		uint16_t raw_max;

		low_bin = agc_find_low_cut_bin(AGC_ROBUST_LOW_CUT_PIXELS);
		high_bin = agc_find_high_cut_bin(AGC_ROBUST_HIGH_CUT_PIXELS);
		/* 极端场景下裁剪区间可能反转，此时退回完整有效范围。 */
		if (high_bin <= low_bin) {
			low_bin = first_bin;
			high_bin = last_bin;
		}

		raw_min = agc_bin_raw_min(low_bin);
		raw_max = agc_bin_raw_max(high_bin);
		g_raw_min = raw_min;
		g_raw_range = raw_max > raw_min ? (uint16_t)(raw_max - raw_min) : 0u;
	}
#elif (AGC_MODE == AGC_MODE_HEQ)
	{
		uint32_t i;
		uint32_t total = 0;
		uint32_t clipped = 0;
		uint32_t cdf = 0;
		uint32_t cdf_min;
		uint32_t denom;
		uint32_t active_bins = (uint32_t)last_bin - first_bin + 1u;

		/* 有效范围之外的 hist 清零，确保 CDF 只由当前热动态范围决定。 */
		for (i = 0; i < first_bin; i++) {
			hist[i] = 0;
		}
		for (i = (uint32_t)last_bin + 1u; i < AGC_BINS; i++) {
			hist[i] = 0;
		}

		/*
		 * Variant HEQ:
		 * 1. 单 bin 超过 CLIP_HIGH 的部分进入 clipped。
		 * 2. 非空 bin 额外加 CLIP_LOW，让稀疏温区也能占到灰度跨度。
		 */
		for (i = first_bin; i <= last_bin; i++) {
			uint16_t n = hist[i];

			if (n > AGC_CLIP_HIGH) {
				clipped += n - AGC_CLIP_HIGH;
				n = AGC_CLIP_HIGH;
			}
			if (n != 0u) n += AGC_CLIP_LOW;
			hist[i] = n;
			total += n;
		}

		/* 被裁掉的像素只在 active_bins 内均匀返还，不把灰度浪费给空温区。 */
		if (clipped != 0u) {
			uint16_t add = (uint16_t)(clipped / active_bins);
			uint16_t rem = (uint16_t)(clipped % active_bins);

			for (i = first_bin; i <= last_bin; i++) {
				hist[i] += add + ((i - first_bin) < rem ? 1u : 0u);
			}
			total += clipped;
		}

		/* 有效范围以下映射到黑端，并做帧间阻尼，避免边界跳变。 */
		for (i = 0; i < first_bin; i++) {
			map8[i] = map_valid ? (uint8_t)((AGC_DAMP_NEW * 0u +
			          (256u - AGC_DAMP_NEW) * prev_map8[i]) >> 8) : 0u;
			prev_map8[i] = map8[i];
		}

		/* CDF 均衡：首个有效 bin 固定为 0，最后有效 bin 固定为 255。 */
		cdf_min = hist[first_bin];
		denom = total > cdf_min ? total - cdf_min : 1u;
		for (i = first_bin; i <= last_bin; i++) {
			uint32_t value;

			if (i == first_bin) {
				value = 0;
				cdf += hist[i];
			} else if (i == last_bin) {
				value = 255;
				cdf += hist[i];
			} else {
				cdf += hist[i];
				value = cdf <= cdf_min ? 0u : ((cdf - cdf_min) * 255u) / denom;
				if (value > 255u) value = 255u;

				/* 帧间映射阻尼：当前帧占 AGC_DAMP_NEW/256。 */
				if (map_valid) {
					value = (AGC_DAMP_NEW * value +
					        (256u - AGC_DAMP_NEW) * prev_map8[i]) >> 8;
				}
			}
			map8[i] = (uint8_t)value;
			prev_map8[i] = map8[i];
		}

		/* 有效范围以上映射到白端，同样参与阻尼。 */
		for (i = (uint32_t)last_bin + 1u; i < AGC_BINS; i++) {
			map8[i] = map_valid ? (uint8_t)((AGC_DAMP_NEW * 255u +
			          (256u - AGC_DAMP_NEW) * prev_map8[i]) >> 8) : 255u;
			prev_map8[i] = map8[i];
		}
		map_valid = 1;
	}
#endif
}

void agc_render(const uint16_t raw14[60][80], uint8_t display8[60][80])
{
	uint32_t y, x;

#if (AGC_MODE == AGC_MODE_ROBUST_MINMAX)
	uint16_t raw;
	uint16_t raw_max;
	uint32_t value;

	raw_max = g_raw_min + g_raw_range;
	for (y = 0; y < 60; y++) {
		for (x = 0; x < 80; x++) {
			if (g_raw_range == 0u) {
				display8[y][x] = AGC_FLAT_OUTPUT;
			} else {
				raw = raw14[y][x] & 0x3FFFu;
				if (raw <= g_raw_min) {
					display8[y][x] = 0;
				} else if (raw >= raw_max) {
					display8[y][x] = 255;
				} else {
					value = ((uint32_t)(raw - g_raw_min) * 255u) / g_raw_range;
					if (value > 255u) value = 255u;
					display8[y][x] = (uint8_t)value;
				}
			}
		}
	}
#elif (AGC_MODE == AGC_MODE_HEQ)
	/* Pass 3: 渲染阶段只做查表，避免在显示路径重复计算 CDF。 */
	for (y = 0; y < 60; y++) {
		for (x = 0; x < 80; x++) {
			display8[y][x] = map8[(raw14[y][x] & 0x3FFFu) >> AGC_SHIFT];
		}
	}
#endif
}
