#ifndef _IMAGE_PROC_H_
#define _IMAGE_PROC_H_

#include <stdint.h>
#include "temp_measure.h"

typedef struct {
	uint32_t frame_count;
	int16_t center_c_x100;
	int16_t max_c_x100;
	int16_t min_c_x100;
} image_proc_stats_t;

void image_proc_init(void);
uint8_t image_pipeline_run(const uint16_t raw14[60][80]);
uint8_t image_proc_has_frame(void);
const uint16_t (*image_proc_get_last_raw14(void))[80];
uint8_t (*image_proc_get_last_gray320(void))[320];
const image_proc_stats_t *image_proc_get_stats(void);
const temp_points_t *image_proc_get_temp_points(void);

#endif
