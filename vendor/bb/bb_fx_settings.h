#ifndef BB_FX_SETTINGS_H
#define BB_FX_SETTINGS_H
#include <stdint.h>
/* Shared effects settings belong to the scene, not an instrument patch.
 * Sends remain independent of note velocity and patch recall. */
typedef enum {
    BB_FX_ECHO_MS, BB_FX_ECHO_FEEDBACK, BB_FX_ECHO_TONE,
    BB_FX_ECHO_DRIVE, BB_FX_ECHO_WEAR, BB_FX_ECHO_WET, BB_FX_ECHO_HEADS,
    BB_FX_SPRING_SIZE, BB_FX_SPRING_DAMP, BB_FX_SPRING_WET,
    BB_FX_DILLO_ECHO, BB_FX_DILLO_SPRING,
    BB_FX_TELEGRAM_ECHO, BB_FX_TELEGRAM_SPRING,
    BB_FX_ECHO_MOTION, BB_FX_PARAMETER_COUNT
} bb_fx_parameter;
typedef struct { uint16_t value[BB_FX_PARAMETER_COUNT]; } bb_fx_settings;
#endif
