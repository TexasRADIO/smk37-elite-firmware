#ifndef BB_OUTPUT_H
#define BB_OUTPUT_H
#include <stdint.h>
/* Stereo-linked sample peak guard. A streaming 32-frame lookahead, not a
 * true-peak/analogue limiter. Never allocate/reset at caller-block boundaries.
 * Input contract: finite, abs <=64; an invalid stereo frame is replaced by
 * silence and counted. Valid gain 1..2, sample rate 8000..192000.
 * Drain by feeding 32 zero frames; do not reset on transport stop (tails).
 * Arrays belong in the audio pool, not on the target interrupt stack. */
#define BB_OUTPUT_DELAY 32u
#define BB_OUTPUT_WINDOW (BB_OUTPUT_DELAY+1u)
#define BB_OUTPUT_CEILING 0.97f
typedef struct {
    float left[BB_OUTPUT_DELAY],right[BB_OUTPUT_DELAY];
    float peaks[BB_OUTPUT_WINDOW];
    uint32_t ages[BB_OUTPUT_WINDOW];
    float boost,gain,release_step,cached_peak,required;
    uint32_t frame,faults,limited;
    unsigned position,head,count;
} bb_output;
int bb_output_init(bb_output *,unsigned sample_rate,float boost);
/* In-place interleaved-independent stereo arrays. Returns 0 without any
 * mutation on invalid pointers. Zero frames is a no-op with valid pointers. */
int bb_output_process(bb_output *,float *left,float *right,unsigned frames);
#endif
