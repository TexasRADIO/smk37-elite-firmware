/* SPDX-License-Identifier: GPL-3.0-only */
#include "fixture.h"

#define HIT(lane) (1u << (lane))
#define ACCENT(lane) { HIT(lane), HIT(lane) }

void elite_fixture(elite *state, int dense)
{
    static const bb_drum_step drum_a[16] = {
        [0] = ACCENT(D8_BD), [2] = { HIT(D8_CH), 0 },
        [4] = { HIT(D8_SD) | HIT(D8_CH), HIT(D8_SD) }, [6] = { HIT(D8_BD), 0 },
        [8] = ACCENT(D8_BD), [10] = { HIT(D8_CH), 0 },
        [12] = { HIT(D8_SD) | HIT(D8_CH), HIT(D8_SD) }, [15] = { HIT(D8_RS), 0 }
    };
    static const bb_drum_step drum_b[12] = {
        [1] = { HIT(D8_CH), 0 }, [3] = ACCENT(D8_OH),
        [5] = { HIT(D8_CP) | HIT(D8_CH), HIT(D8_CP) }, [7] = { HIT(D8_LT), 0 },
        [9] = { HIT(D8_CB), 0 }, [11] = { HIT(D8_CP) | HIT(D8_CH), 0 }
    };
    static const bb_note bass[16] = {
        [0] = {36, 1, 1, 0}, [2] = {36, 1, 0, 1}, [3] = {43, 1, 0, 0},
        [5] = {39, 1, 1, 0}, [7] = {36, 1, 0, 0}, [8] = {36, 1, 1, 0},
        [10] = {46, 1, 0, 1}, [11] = {43, 1, 0, 0}, [14] = {39, 1, 0, 0}
    };
    static const bb_note lead[16] = {
        [1] = {67, 1, 0, 0}, [4] = {70, 1, 1, 1}, [5] = {72, 1, 0, 0},
        [9] = {67, 1, 0, 0}, [12] = {63, 1, 1, 0}, [14] = {65, 1, 0, 0}
    };
    static const bb_drum_step all_lanes = {
        (1u << D8_NUM) - 1u, (1u << D8_NUM) - 1u
    };
    elite_init(state);
    bb_set_tempo(&state->clock, dense ? 300 : 118);
    bb_set_swing(&state->clock, dense ? 75 : 54);
    elite_set_drum_kit(state, ELITE_DRUM_B, 3);
    elite_set_synth_patch(state, 0, dense ? 6 : 4);
    elite_set_synth_patch(state, 1, 3);
    elite_set_drum_pattern(state, ELITE_DRUM_A, dense ? &all_lanes : drum_a, dense ? 1 : 16);
    elite_set_drum_pattern(state, ELITE_DRUM_B, dense ? &all_lanes : drum_b, dense ? 1 : 12);
    elite_set_notes(state, 0, bass, 16);
    elite_set_notes(state, 1, lead, 16);
}
