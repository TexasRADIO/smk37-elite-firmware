/* SPDX-License-Identifier: GPL-3.0-only */
#include "../fixture.h"

/* Serial callable entry points for the link proof. The caller must load the
 * ELF sections and supply a stack. No reset handler or peripheral access. */
static elite state __attribute__((section(".pool"), aligned(32)));
static int32_t output[FM1_BLOCK_WORDS] __attribute__((aligned(32)));

#define PROBE_API __attribute__((section(".text.probe_api"), used))

PROBE_API void elite_pi32_configure(int dense)
{
    elite_fixture(&state, dense);
}

PROBE_API void elite_pi32_start(void)
{
    elite_start(&state);
}

PROBE_API void elite_pi32_stop(void)
{
    elite_stop(&state);
}

PROBE_API const int32_t *elite_pi32_block(void)
{
    return elite_render(&state, output, FM1_BLOCK_FRAMES) == 0 ? output : 0;
}
