/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_LIBRARY_REGION_H
#define ELITE_LIBRARY_REGION_H
/* Historical backup shows these sectors erased. Runtime ownership and chip
 * checks are still mandatory; this constant alone never authorizes a write. */
#define ELITE_LIBRARY_LO 0x000A0000u
#define ELITE_LIBRARY_HI 0x000A8000u
#endif
