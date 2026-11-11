#ifndef BB_TELEGRAM_H
#define BB_TELEGRAM_H
#include "bb_core.h"
/* First native port of Studio's legacy mono Telegram musical architecture.
 * Not an SH-101 circuit model or a completed port of Studio advanced modes. */
typedef struct {
    bb_telegram_patch sound;
    float phase, sub_phase, lfo_phase, pitch, target_pitch, pitch_ratio;
    float amp, amp_target, attack_c, release_c, wave_mix[3], mix_c;
    float cutoff, cutoff_ratio, ic1, ic2, a1, a2, a3, damping;
    uint32_t gate_left, glide_left, filter_left, filter_total;
    uint8_t control_phase, previous_note,slide_out;
} bb_telegram;
void bb_telegram_init(bb_telegram *);
int bb_telegram_sound(bb_telegram *,const bb_telegram_patch *);
void bb_telegram_step(bb_telegram *,bb_note,uint32_t frames);
/* Held key, explicit overlap legato. Filter/glide time uses finite phrase
 * duration, independently of the indefinitely held gate. Velocity >=100
 * selects native accent. Invalid input leaves state unchanged. */
int bb_telegram_key(bb_telegram *,unsigned note,unsigned velocity,int legato,
                    uint32_t phrase_frames);
void bb_telegram_stop(bb_telegram *);
void bb_telegram_render(bb_telegram *,float *,uint32_t frames);
#endif
