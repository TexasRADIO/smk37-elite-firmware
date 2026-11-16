/* SPDX-License-Identifier: GPL-3.0-only */
#include "fm1_cc.h"
#include "bass303.h"
#include "bb_telegram.h"
#include "bb_output.h"

/* PI32 direct calls cannot reach these functions from the XIP window. */
void elite_far_bass303_render(bass303_t *voice, float *out, int frames)
{
    FM1_FAR(bass303_render)(voice, out, frames);
}

void elite_far_bb_telegram_render(bb_telegram *voice, float *out, uint32_t frames)
{
    FM1_FAR(bb_telegram_render)(voice, out, frames);
}

int elite_far_bb_output_process(bb_output *state, float *left, float *right, unsigned frames)
{
    return FM1_FAR(bb_output_process)(state, left, right, frames);
}
