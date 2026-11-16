/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_DRY_BUS_H
#define ELITE_DRY_BUS_H

/* The combined board uses compact mono side buses. Standalone Dillo
 * still adds its dry signal to a stereo synth buffer. */
#ifndef CONDUCTOR_DRY_STRIDE
#define CONDUCTOR_DRY_STRIDE 2u
#endif

_Static_assert(CONDUCTOR_DRY_STRIDE == 1u || CONDUCTOR_DRY_STRIDE == 2u,
               "Conductor dry buses must be mono or dual mono");

#endif
