#ifndef _TEMP_MEASURE_H_
#define _TEMP_MEASURE_H_

#include <stdint.h>
#include "lepton.h"

typedef enum {
    TEMP_POINT_CENTER = 0,
    TEMP_POINT_MAX,
    TEMP_POINT_MIN,
    TEMP_POINT_USER1,
    TEMP_POINT_USER2,
    TEMP_POINT_COUNT,
} temp_point_id_t;

typedef struct {
    uint8_t enabled;
    uint8_t x;
    uint8_t y;
    int16_t temp_c_x100;
} temp_point_t;

typedef struct {
    temp_point_t point[TEMP_POINT_COUNT];
} temp_points_t;

void    temp_init(const lep_rbfo_t *rbfo, uint8_t valid);
void    temp_set_calibration(float gain, float offset);
void    temp_set_emissivity(uint16_t emiss_x100);
void    temp_set_ambient_c_x100(int16_t ambient_c_x100);
uint16_t temp_get_emissivity(void);
void    convert_frame(const uint16_t raw14[60][80], int16_t temp_c_x100[60][80]);
void    temp_measure_points(const uint16_t raw14[60][80],
                            int16_t *center_c_x100,
                            int16_t *max_c_x100,
                            int16_t *min_c_x100);
void    temp_measure_points_full(const uint16_t raw14[60][80],
                                 temp_points_t *points);
const temp_points_t *temp_get_points(void);
void    temp_set_point_enabled(temp_point_id_t id, uint8_t enabled);
uint8_t temp_get_point_enabled(temp_point_id_t id);
void    temp_set_user_point(temp_point_id_t id, uint8_t x, uint8_t y);
void    temp_get_user_point(temp_point_id_t id, uint8_t *x, uint8_t *y);

int16_t temp_get_center_roi(const int16_t temp_c_x100[60][80]);
int16_t temp_get_max(const int16_t temp_c_x100[60][80]);
int16_t temp_get_min(const int16_t temp_c_x100[60][80]);

#endif
