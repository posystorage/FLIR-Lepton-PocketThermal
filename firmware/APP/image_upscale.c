#include "image_upscale.h"
#include "memory_sections.h"
#include <string.h>

#if (IMAGE_UPSCALE_MODE == IMAGE_UPSCALE_SR_2X2_LIMITED)

/*
 * 超分辨率路径处理 AGC 后的 Gray8[60][80]，输出 Gray8[240][320]。
 *
 * 当前实现为“两级 2x + 轻量受限锐化”：
 * 1. 第一级 80x60 -> 160x120 使用方向感知 2x 插值，尽量保留原始热边界。
 * 2. 第二级 160x120 -> 320x240 默认使用轻量双线性/平均路径，降低耗时。
 * 3. 最后可选 LIGHT 锐化，只允许很小幅度变化，避免红外图像出现假描边。
 *
 * 说明：热像目标通常有扩散特性，第二级不再强行做方向判断，画面更柔和，
 * 同时显著减少 Catmull-Rom 和对角线判断的调用次数。
 */
#define IMAGE_SR_DIR_TIE_THRESHOLD      4u
#define IMAGE_SR_SHARPEN_DIV            8
#define IMAGE_SR_SHARPEN_LIMIT          3
#define IMAGE_SR_FAST_BUFFER_ENABLE     1
#define IMAGE_SR_RAMFUNC_ENABLE         0
#define IMAGE_SR_FAST_SCALE2_ENABLE     1

#if defined(__CC_ARM)
#define IMAGE_UPSCALE_INLINE            static __forceinline
#else
#define IMAGE_UPSCALE_INLINE            static inline
#endif

/* IMAGE_SR_RAMFUNC 保留为实验开关；当前实测函数搬 RAM 不提升速度，默认关闭。 */
#if defined(__CC_ARM) && (IMAGE_SR_RAMFUNC_ENABLE)
#define IMAGE_SR_RAMFUNC                FW_RAMFUNC0
#else
#define IMAGE_SR_RAMFUNC
#endif

/* 高频缓存默认放 RAM0。实测 RAM0 访问快于 RAM1，因此不强制放到 SRAM1。 */
#if defined(__CC_ARM) && (IMAGE_SR_FAST_BUFFER_ENABLE)
#define IMAGE_SR_FAST_ZI                FW_RAM0_ZI
#else
#define IMAGE_SR_FAST_ZI
#endif

typedef enum {
	IMAGE_UPSCALE_STAGE_IDLE = 0,
	IMAGE_UPSCALE_STAGE_SCALE1,
	IMAGE_UPSCALE_STAGE_SCALE2,
	IMAGE_UPSCALE_STAGE_SHARPEN,
	IMAGE_UPSCALE_STAGE_DONE
} image_upscale_stage_t;

/* 中间灰度缓存：g_sr_mid 为第一级结果，g_sr_out 为最终 320x240 结果。 */
static uint8_t g_sr_mid[IMAGE_UPSCALE_MID_H][IMAGE_UPSCALE_MID_W] IMAGE_SR_FAST_ZI;
static uint8_t g_sr_out[IMAGE_UPSCALE_OUT_H][IMAGE_UPSCALE_OUT_W];

#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME)
static uint8_t g_sr_sharp[IMAGE_UPSCALE_OUT_H][IMAGE_UPSCALE_OUT_W];
#endif

#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT)
/*
 * LIGHT 锐化采用 3 行环形缓存。
 * 这样可在 g_sr_out 上原地写回锐化结果，避免再分配 320x240 的锐化整帧缓存。
 */
static uint8_t g_sr_sharp_line0[IMAGE_UPSCALE_OUT_W] IMAGE_SR_FAST_ZI;
static uint8_t g_sr_sharp_line1[IMAGE_UPSCALE_OUT_W] IMAGE_SR_FAST_ZI;
static uint8_t g_sr_sharp_line2[IMAGE_UPSCALE_OUT_W] IMAGE_SR_FAST_ZI;
static uint8_t *g_sr_sharp_prev;
static uint8_t *g_sr_sharp_curr;
static uint8_t *g_sr_sharp_next;
#endif

static uint8_t (*g_src)[IMAGE_UPSCALE_SRC_W];
static image_upscale_stage_t g_stage = IMAGE_UPSCALE_STAGE_IDLE;
static uint32_t g_stage_y = 0;

IMAGE_UPSCALE_INLINE uint32_t clamp_index_u32(uint32_t v, uint32_t max_v)
{
	return (v > max_v) ? max_v : v;
}

/* 受限插值和锐化都需要大量 min/max，保持为 inline 避免函数调用开销。 */
IMAGE_UPSCALE_INLINE int32_t abs_i32(int32_t v)
{
	return (v < 0) ? -v : v;
}

IMAGE_UPSCALE_INLINE uint8_t min_u8(uint8_t a, uint8_t b)
{
	return (a < b) ? a : b;
}

IMAGE_UPSCALE_INLINE uint8_t max_u8(uint8_t a, uint8_t b)
{
	return (a > b) ? a : b;
}

IMAGE_UPSCALE_INLINE uint8_t avg2_u8(uint8_t a, uint8_t b)
{
	return (uint8_t)(((uint32_t)a + (uint32_t)b + 1u) >> 1);
}

IMAGE_UPSCALE_INLINE uint8_t avg4_u8(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return (uint8_t)(((uint32_t)a + (uint32_t)b + (uint32_t)c + (uint32_t)d + 2u) >> 2);
}

IMAGE_UPSCALE_INLINE uint8_t clamp_u8_i32(int32_t v)
{
	if (v < 0) {
		return 0u;
	}
	if (v > 255) {
		return 255u;
	}
	return (uint8_t)v;
}

IMAGE_UPSCALE_INLINE uint8_t clamp_range_i32(int32_t v, uint8_t low, uint8_t high)
{
	if (v < (int32_t)low) {
		return low;
	}
	if (v > (int32_t)high) {
		return high;
	}
	return (uint8_t)v;
}

IMAGE_UPSCALE_INLINE uint8_t sample_u8(uint8_t *src, uint32_t w, uint32_t h,
                                       int32_t x, int32_t y)
{
	uint32_t sx, sy;

	if (x < 0) {
		sx = 0u;
	} else {
		sx = clamp_index_u32((uint32_t)x, w - 1u);
	}

	if (y < 0) {
		sy = 0u;
	} else {
		sy = clamp_index_u32((uint32_t)y, h - 1u);
	}

	return src[sy * w + sx];
}

/*
 * Catmull-Rom 半像素插值：
 *   v = (-p0 + 9*p1 + 9*p2 - p3) / 16
 * 然后把结果限制在 p1/p2 范围内。这样比线性更锐，但不会过冲到邻域外。
 */
IMAGE_UPSCALE_INLINE uint8_t catmull_rom_half_limited(uint8_t p0, uint8_t p1,
                                                      uint8_t p2, uint8_t p3)
{
	int32_t v;
	uint8_t low, high;

	v = -(int32_t)p0 + 9 * (int32_t)p1 + 9 * (int32_t)p2 - (int32_t)p3;
	v = (v + 8) / 16;
	low = min_u8(p1, p2);
	high = max_u8(p1, p2);
	return clamp_range_i32(v, low, high);
}

IMAGE_UPSCALE_INLINE uint8_t center_directional_fast(const uint8_t *row_m1,
                                                     const uint8_t *row0,
                                                     const uint8_t *row1,
                                                     const uint8_t *row2,
                                                     uint32_t x)
{
	uint8_t nw, ne, sw, se;
	uint32_t grad_nw_se, grad_ne_sw;
	int32_t mix;

	/*
	 * 中心点位于四个源像素之间。
	 * 比较两条对角线变化量，沿变化较小的方向插值，避免跨边缘平均。
	 */
	nw = row0[x];
	ne = row0[x + 1u];
	sw = row1[x];
	se = row1[x + 1u];
	grad_nw_se = (uint32_t)abs_i32((int32_t)nw - (int32_t)se);
	grad_ne_sw = (uint32_t)abs_i32((int32_t)ne - (int32_t)sw);

	if (grad_nw_se + IMAGE_SR_DIR_TIE_THRESHOLD < grad_ne_sw) {
		return catmull_rom_half_limited(row_m1[x - 1u], nw, se, row2[x + 2u]);
	}

	if (grad_ne_sw + IMAGE_SR_DIR_TIE_THRESHOLD < grad_nw_se) {
		return catmull_rom_half_limited(row_m1[x + 2u], ne, sw, row2[x - 1u]);
	}

	/* 对角线差异接近时，说明方向不明确，退回四点平均，减少伪边缘。 */
	mix = (int32_t)nw + (int32_t)ne + (int32_t)sw + (int32_t)se + 2;
	return clamp_u8_i32(mix >> 2);
}

/* 边界像素无法直接访问完整邻域，使用带 clamp 的通用慢路径。 */
static uint8_t center_directional_slow(uint8_t *src, uint32_t w, uint32_t h,
                                       uint32_t x, uint32_t y)
{
	uint8_t nw, ne, sw, se;
	uint32_t grad_nw_se, grad_ne_sw;
	uint8_t v0, v3;
	int32_t mix;

	nw = sample_u8(src, w, h, (int32_t)x,     (int32_t)y);
	ne = sample_u8(src, w, h, (int32_t)x + 1, (int32_t)y);
	sw = sample_u8(src, w, h, (int32_t)x,     (int32_t)y + 1);
	se = sample_u8(src, w, h, (int32_t)x + 1, (int32_t)y + 1);
	grad_nw_se = (uint32_t)abs_i32((int32_t)nw - (int32_t)se);
	grad_ne_sw = (uint32_t)abs_i32((int32_t)ne - (int32_t)sw);

	if (grad_nw_se + IMAGE_SR_DIR_TIE_THRESHOLD < grad_ne_sw) {
		v0 = sample_u8(src, w, h, (int32_t)x - 1, (int32_t)y - 1);
		v3 = sample_u8(src, w, h, (int32_t)x + 2, (int32_t)y + 2);
		return catmull_rom_half_limited(v0, nw, se, v3);
	}

	if (grad_ne_sw + IMAGE_SR_DIR_TIE_THRESHOLD < grad_nw_se) {
		v0 = sample_u8(src, w, h, (int32_t)x + 2, (int32_t)y - 1);
		v3 = sample_u8(src, w, h, (int32_t)x - 1, (int32_t)y + 2);
		return catmull_rom_half_limited(v0, ne, sw, v3);
	}

	mix = (int32_t)nw + (int32_t)ne + (int32_t)sw + (int32_t)se + 2;
	return clamp_u8_i32(mix >> 2);
}

static void scale2x_pixel_slow(uint8_t *src, uint32_t src_w, uint32_t src_h,
                               uint8_t *dst, uint32_t x, uint32_t y)
{
	uint32_t dst_w;
	uint32_t dx, dy;
	uint8_t p0, p1, p2, p3;

	dst_w = src_w * 2u;
	dx = x * 2u;
	dy = y * 2u;
	p1 = sample_u8(src, src_w, src_h, (int32_t)x, (int32_t)y);
	dst[dy * dst_w + dx] = p1;

	/* 输出偶数坐标复制源像素；水平/垂直半像素使用受限 Catmull-Rom。 */
	p0 = sample_u8(src, src_w, src_h, (int32_t)x - 1, (int32_t)y);
	p2 = sample_u8(src, src_w, src_h, (int32_t)x + 1, (int32_t)y);
	p3 = sample_u8(src, src_w, src_h, (int32_t)x + 2, (int32_t)y);
	dst[dy * dst_w + dx + 1u] =
		catmull_rom_half_limited(p0, p1, p2, p3);

	p0 = sample_u8(src, src_w, src_h, (int32_t)x, (int32_t)y - 1);
	p2 = sample_u8(src, src_w, src_h, (int32_t)x, (int32_t)y + 1);
	p3 = sample_u8(src, src_w, src_h, (int32_t)x, (int32_t)y + 2);
	dst[(dy + 1u) * dst_w + dx] =
		catmull_rom_half_limited(p0, p1, p2, p3);

	dst[(dy + 1u) * dst_w + dx + 1u] =
		center_directional_slow(src, src_w, src_h, x, y);
}

/*
 * 通用方向感知 2x 行扩展。
 * 内部区域使用直接行指针，边界列/边界行交给 slow path 处理。
 */
static void scale2x_row_fast(uint8_t *src, uint32_t src_w, uint32_t src_h,
                             uint8_t *dst, uint32_t y)
{
	uint32_t x;
	uint32_t dst_w;
	uint8_t *dst0;
	uint8_t *dst1;
	uint8_t *row_m1;
	uint8_t *row0;
	uint8_t *row1;
	uint8_t *row2;
	uint32_t dx;
	uint8_t p1;

	if ((y == 0u) || ((y + 2u) >= src_h)) {
		for (x = 0; x < src_w; x++) {
			scale2x_pixel_slow(src, src_w, src_h, dst, x, y);
		}
		return;
	}

	dst_w = src_w * 2u;
	dst0 = dst + (y * 2u) * dst_w;
	dst1 = dst0 + dst_w;
	row_m1 = src + (y - 1u) * src_w;
	row0 = src + y * src_w;
	row1 = row0 + src_w;
	row2 = row1 + src_w;

	scale2x_pixel_slow(src, src_w, src_h, dst, 0u, y);

	for (x = 1u; (x + 2u) < src_w; x++) {
		dx = x * 2u;
		p1 = row0[x];

		dst0[dx] = p1;
		dst0[dx + 1u] =
			catmull_rom_half_limited(row0[x - 1u], p1, row0[x + 1u], row0[x + 2u]);
		dst1[dx] =
			catmull_rom_half_limited(row_m1[x], p1, row1[x], row2[x]);
		dst1[dx + 1u] = center_directional_fast(row_m1, row0, row1, row2, x);
	}

	scale2x_pixel_slow(src, src_w, src_h, dst, src_w - 2u, y);
	scale2x_pixel_slow(src, src_w, src_h, dst, src_w - 1u, y);
}

static void scale1_row(uint32_t y)
{
	/* 第一级保留完整方向感知算法，优先保护 80x60 原始热边界。 */
	scale2x_row_fast(&g_src[0][0], IMAGE_UPSCALE_SRC_W,
	                 IMAGE_UPSCALE_SRC_H, &g_sr_mid[0][0], y);
}

#if (IMAGE_SR_FAST_SCALE2_ENABLE)
/*
 * 第二级轻量 2x：
 * - 偶数坐标复制 160x120 中间图。
 * - 水平/垂直半像素做 2 点平均。
 * - 中心像素做 4 点平均。
 *
 * 该路径比再次执行方向感知 Catmull-Rom 更柔和，也更符合红外热量扩散观感。
 */
static void scale2_row_light(uint32_t y)
{
	const uint8_t *src0;
	const uint8_t *src1;
	uint8_t *dst0;
	uint8_t *dst1;
	uint32_t x;
	uint8_t p, e, s, se;

	src0 = g_sr_mid[y];
	if ((y + 1u) < IMAGE_UPSCALE_MID_H) {
		src1 = g_sr_mid[y + 1u];
	} else {
		src1 = src0;
	}

	dst0 = g_sr_out[y * 2u];
	dst1 = dst0 + IMAGE_UPSCALE_OUT_W;

	/* 指针递增写两个输出行，避免每个像素重复计算 x*2 和行偏移。 */
	for (x = 0; (x + 1u) < IMAGE_UPSCALE_MID_W; x++) {
		p = src0[0];
		e = src0[1];
		s = src1[0];
		se = src1[1];

		dst0[0] = p;
		dst0[1] = avg2_u8(p, e);
		dst1[0] = avg2_u8(p, s);
		dst1[1] = avg4_u8(p, e, s, se);

		src0++;
		src1++;
		dst0 += 2;
		dst1 += 2;
	}

	/* 最后一列没有 east 邻居，复制边界并保留垂直平均。 */
	p = src0[0];
	s = src1[0];
	e = avg2_u8(p, s);
	dst0[0] = p;
	dst0[1] = p;
	dst1[0] = e;
	dst1[1] = e;
}
#endif

static void scale2_row(uint32_t y)
{
#if (IMAGE_SR_FAST_SCALE2_ENABLE)
	/* 默认快速路径；关闭 IMAGE_SR_FAST_SCALE2_ENABLE 可回退到完整方向感知第二级。 */
	scale2_row_light(y);
#else
	scale2x_row_fast(&g_sr_mid[0][0], IMAGE_UPSCALE_MID_W,
	                 IMAGE_UPSCALE_MID_H, &g_sr_out[0][0], y);
#endif
}

#if (IMAGE_UPSCALE_SHARPEN_MODE != IMAGE_UPSCALE_SHARPEN_OFF)
IMAGE_UPSCALE_INLINE int32_t sharpen_light_div(int32_t lap)
{
#if (IMAGE_SR_SHARPEN_DIV == 8)
	/* 保持向零截断，与有符号除法 lap/8 的效果一致。 */
	if (lap >= 0) {
		return lap >> 3;
	}
	return -((-lap) >> 3);
#else
	return lap / IMAGE_SR_SHARPEN_DIV;
#endif
}

IMAGE_UPSCALE_INLINE uint8_t sharpen_light_limited(uint8_t c, uint8_t n,
                                                   uint8_t s, uint8_t wv,
                                                   uint8_t e)
{
	uint8_t bound;
	int32_t lap, delta, v;

	/*
	 * 4 邻域拉普拉斯轻锐化：
	 * delta = (4*c - n - s - w - e) / IMAGE_SR_SHARPEN_DIV
	 * 再限制 delta 幅度，并限制最终值不超过邻域边界，避免高亮/低亮过冲。
	 */
	lap = ((int32_t)c << 2) - (int32_t)n - (int32_t)s - (int32_t)wv - (int32_t)e;
	delta = sharpen_light_div(lap);
	if (delta == 0) {
		return c;
	}

	if (delta > 0) {
		if (delta > IMAGE_SR_SHARPEN_LIMIT) {
			delta = IMAGE_SR_SHARPEN_LIMIT;
		}
		bound = max_u8(max_u8(n, s), max_u8(wv, e));
		bound = max_u8(bound, c);
		v = (int32_t)c + delta;
		return (v > (int32_t)bound) ? bound : (uint8_t)v;
	}

	if (delta < -IMAGE_SR_SHARPEN_LIMIT) {
		delta = -IMAGE_SR_SHARPEN_LIMIT;
	}
	bound = min_u8(min_u8(n, s), min_u8(wv, e));
	bound = min_u8(bound, c);
	v = (int32_t)c + delta;
	return (v < (int32_t)bound) ? bound : (uint8_t)v;
}

uint8_t image_upscale_sharpen_light_at(uint8_t src[IMAGE_UPSCALE_OUT_H][IMAGE_UPSCALE_OUT_W],
                                       uint32_t x, uint32_t y)
{
	uint8_t c, n, s, wv, e;

	c = src[y][x];
	n = src[(y == 0u) ? 0u : (y - 1u)][x];
	s = src[(y + 1u >= IMAGE_UPSCALE_OUT_H) ? (IMAGE_UPSCALE_OUT_H - 1u) : (y + 1u)][x];
	wv = src[y][(x == 0u) ? 0u : (x - 1u)];
	e = src[y][(x + 1u >= IMAGE_UPSCALE_OUT_W) ? (IMAGE_UPSCALE_OUT_W - 1u) : (x + 1u)];

	return sharpen_light_limited(c, n, s, wv, e);
}
#endif

#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT)
static void sharpen_light_prepare_inplace(void)
{
	/* 初始化三行缓存：第 0 行向上复制边界，第 1 行作为 next。 */
	g_sr_sharp_prev = g_sr_sharp_line0;
	g_sr_sharp_curr = g_sr_sharp_line1;
	g_sr_sharp_next = g_sr_sharp_line2;

	memcpy(g_sr_sharp_prev, g_sr_out[0], IMAGE_UPSCALE_OUT_W);
	memcpy(g_sr_sharp_curr, g_sr_out[0], IMAGE_UPSCALE_OUT_W);
	memcpy(g_sr_sharp_next, g_sr_out[1], IMAGE_UPSCALE_OUT_W);
}

static void IMAGE_SR_RAMFUNC sharpen_light_row_inplace(uint32_t y)
{
	const uint8_t *prev;
	const uint8_t *curr;
	const uint8_t *next;
	uint8_t *dst;
	uint32_t x;
	uint32_t last;
	uint8_t wv, c, e;

	prev = g_sr_sharp_prev;
	curr = g_sr_sharp_curr;
	next = g_sr_sharp_next;
	dst = g_sr_out[y];
	last = IMAGE_UPSCALE_OUT_W - 1u;

	/* 首尾列使用边界复制；中间列滚动维护 w/c/e，减少每像素重复加载。 */
	dst[0] = sharpen_light_limited(curr[0], prev[0], next[0], curr[0], curr[1]);

	wv = curr[0];
	c = curr[1];
	e = curr[2];
	for (x = 1u; (x + 2u) < IMAGE_UPSCALE_OUT_W; x++) {
		dst[x] = sharpen_light_limited(c, prev[x], next[x], wv, e);
		wv = c;
		c = e;
		e = curr[x + 2u];
	}

	dst[x] = sharpen_light_limited(c, prev[x], next[x], wv, e);
	dst[last] = sharpen_light_limited(curr[last], prev[last], next[last],
	                                  curr[last - 1u], curr[last]);
}

static void sharpen_light_advance_inplace(uint32_t y)
{
	uint8_t *old_prev;

	if ((y + 1u) >= IMAGE_UPSCALE_OUT_H) {
		return;
	}

	old_prev = g_sr_sharp_prev;
	g_sr_sharp_prev = g_sr_sharp_curr;
	g_sr_sharp_curr = g_sr_sharp_next;
	g_sr_sharp_next = old_prev;

	/* 环形复用最旧的一行缓存，装入下一行原始未锐化数据。 */
	if ((y + 2u) < IMAGE_UPSCALE_OUT_H) {
		memcpy(g_sr_sharp_next, g_sr_out[y + 2u], IMAGE_UPSCALE_OUT_W);
	} else {
		memcpy(g_sr_sharp_next, g_sr_sharp_curr, IMAGE_UPSCALE_OUT_W);
	}
}
#endif

#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME)
static void sharpen_light_row(uint32_t y)
{
	uint32_t x;

	for (x = 0; x < IMAGE_UPSCALE_OUT_W; x++) {
		g_sr_sharp[y][x] = image_upscale_sharpen_light_at(g_sr_out, x, y);
	}
}
#endif

void image_upscale_start(uint8_t src[IMAGE_UPSCALE_SRC_H][IMAGE_UPSCALE_SRC_W])
{
	/* 分片状态机入口。后续可用 image_upscale_step(row_budget) 做时间片调用。 */
	g_src = src;
	g_stage = IMAGE_UPSCALE_STAGE_SCALE1;
	g_stage_y = 0;
}

uint8_t image_upscale_step(uint32_t row_budget)
{
	/*
	 * 每次 step 最多推进 row_budget 行，便于后续把整帧超分拆成状态机时间片。
	 * 同步接口 image_upscale_sr320() 传入 0xFFFFFFFF，一次跑完整帧。
	 */
	if (row_budget == 0u) {
		row_budget = 1u;
	}

	while (row_budget != 0u) {
		switch (g_stage) {
		case IMAGE_UPSCALE_STAGE_SCALE1:
			scale1_row(g_stage_y);
			g_stage_y++;
			row_budget--;
			if (g_stage_y >= IMAGE_UPSCALE_SRC_H) {
				g_stage = IMAGE_UPSCALE_STAGE_SCALE2;
				g_stage_y = 0;
			}
			break;

		case IMAGE_UPSCALE_STAGE_SCALE2:
			scale2_row(g_stage_y);
			g_stage_y++;
			row_budget--;
			if (g_stage_y >= IMAGE_UPSCALE_MID_H) {
#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT)
				sharpen_light_prepare_inplace();
				g_stage = IMAGE_UPSCALE_STAGE_SHARPEN;
				g_stage_y = 0;
#elif (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME)
				g_stage = IMAGE_UPSCALE_STAGE_SHARPEN;
				g_stage_y = 0;
#else
				g_stage = IMAGE_UPSCALE_STAGE_DONE;
#endif
			}
			break;

#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT) || \
    (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME)
		case IMAGE_UPSCALE_STAGE_SHARPEN:
#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT)
			sharpen_light_row_inplace(g_stage_y);
			sharpen_light_advance_inplace(g_stage_y);
#else
			sharpen_light_row(g_stage_y);
#endif
			g_stage_y++;
			row_budget--;
			if (g_stage_y >= IMAGE_UPSCALE_OUT_H) {
				g_stage = IMAGE_UPSCALE_STAGE_DONE;
			}
			break;
#endif

		case IMAGE_UPSCALE_STAGE_DONE:
			return 1u;

		case IMAGE_UPSCALE_STAGE_IDLE:
		default:
			return 0u;
		}
	}

	return (g_stage == IMAGE_UPSCALE_STAGE_DONE) ? 1u : 0u;
}

uint8_t (*image_upscale_get_result(void))[IMAGE_UPSCALE_OUT_W]
{
#if (IMAGE_UPSCALE_SHARPEN_MODE == IMAGE_UPSCALE_SHARPEN_LIGHT_FRAME)
	return g_sr_sharp;
#else
	return g_sr_out;
#endif
}

uint8_t (*image_upscale_sr320(uint8_t src[IMAGE_UPSCALE_SRC_H][IMAGE_UPSCALE_SRC_W]))
	[IMAGE_UPSCALE_OUT_W]
{
	/* 兼容旧同步调用方式：启动状态机后一次性跑到 DONE。 */
	image_upscale_start(src);
	while (!image_upscale_step(0xFFFFFFFFu)) {
	}
	return image_upscale_get_result();
}

#endif
