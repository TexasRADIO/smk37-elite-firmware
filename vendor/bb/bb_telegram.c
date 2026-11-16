/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat native implementation of the musical structure in Studio
 * telegramVoice.ts (legacy mono path), with original DSP and licensed X0X
 * fastmath helpers. Source recipes remain attributed in bb_core.c.
 * PolyBLEP oscillators and a two-pole state-variable lowpass are approximations,
 * not Web Audio sample equivalence or an SH-101 transistor-level model. */
#include "bb_telegram.h"
#include "fastmath.h"
#include <string.h>
#define TG_SR 44100.0f
static float tg_rc(float ms){return fm_expf(-1000.0f/(ms*TG_SR));}
static uint32_t fraction(uint32_t n,unsigned p){return n/100u*p+(n%100u)*p/100u;}
static float blep(float t,float dt) {
    if(t<dt){t/=dt;return t+t-t*t-1.0f;}
    if(t>1.0f-dt){t=(t-1.0f)/dt;return t*t+t+t+1.0f;}
    return 0;
}
static float pulse(float t,float dt) {
    float shifted=t+0.5f;if(shifted>=1)shifted-=1;
    return (t<0.5f?1.0f:-1.0f)+blep(t,dt)-blep(shifted,dt);
}
int bb_telegram_sound(bb_telegram *v,const bb_telegram_patch *p) {
    if(!v || !bb_telegram_patch_valid(p))return 0;
    /* Preserve the note's elapsed envelope time when a performance knob or
     * patch changes its filter. Do not retrigger amplitude, oscillator phase,
     * glide or filter integrators. Expensive math runs only on sound changes. */
    if(v->sound.cutoff_hz>=20 && (v->sound.cutoff_hz!=p->cutoff_hz || v->sound.envelope!=p->envelope)) {
        float base=fm_minf((float)p->cutoff_hz,16000.0f);
        float top=fm_minf(16000.0f,base+(float)p->envelope*45.0f);
        if(v->filter_left && v->filter_total && top>base) {
            float elapsed=1.0f-(float)v->filter_left/(float)v->filter_total;
            v->cutoff=top*fm_exp2f(fm_log2f(base/top)*elapsed);
            v->cutoff=fm_clampf(v->cutoff,base,top);
            v->cutoff_ratio=fm_exp2f(fm_log2f(base/v->cutoff)/(float)v->filter_left);
        } else {v->cutoff=base;v->cutoff_ratio=1.0f;}
    }
    v->sound=*p;v->attack_c=tg_rc((float)p->attack_ms);
    v->release_c=tg_rc((float)p->release_ms);
    v->damping=1.0f/fm_maxf(0.1f,(float)p->resonance_x100*0.01f);
    v->control_phase=0;return 1;
}
void bb_telegram_init(bb_telegram *v) {
    memset(v,0,sizeof *v);v->pitch=v->target_pitch=261.625565f;
    v->pitch_ratio=v->cutoff_ratio=1;v->mix_c=tg_rc(10);
    bb_telegram_sound(v,&bb_telegram_bank[0].sound);
    v->cutoff=(float)v->sound.cutoff_hz;
    v->wave_mix[0]=v->sound.square*0.01f;v->wave_mix[1]=v->sound.saw*0.01f;v->wave_mix[2]=v->sound.sub*0.01f;
}
void bb_telegram_stop(bb_telegram *v) {
    v->gate_left=0;v->amp_target=0;v->previous_note=0;v->glide_left=0;v->slide_out=0;
}
void bb_telegram_step(bb_telegram *v,bb_note note,uint32_t frames) {
    float hz,top,base;
    uint32_t glide;
    unsigned tied=v->previous_note && v->slide_out;
    if(!note.gate || !frames){bb_telegram_stop(v);return;}
    hz=440.0f*fm_exp2f(((float)note.note-69.0f)/12.0f);
    hz=fm_clampf(hz,8.0f,TG_SR*0.45f);v->target_pitch=hz;
    glide=(uint32_t)((float)v->sound.glide_ms*(TG_SR/1000.0f));
    if(glide>fraction(frames,95))glide=fraction(frames,95);
    /* Studio legacy glide follows adjacent played steps, including their gate
     * gaps; a rest clears it. A future legato-only mode will be explicit. */
    if((tied || v->sound.glide) && v->previous_note && glide) {
        v->glide_left=glide;v->pitch_ratio=fm_exp2f(fm_log2f(hz/v->pitch)/(float)glide);
    } else {v->pitch=hz;v->glide_left=0;v->pitch_ratio=1;}
    v->previous_note=1;v->slide_out=note.slide;
    v->gate_left=note.slide?frames:fraction(frames,82);if(!v->gate_left)v->gate_left=1;
    v->amp_target=note.accent?0.16f:0.115f;
    /* Explicit outgoing step slides tie into the next gated note without
     * retriggering filter/amp memory. Legacy global adjacent-note glide is
     * unchanged when step slides are absent. A rest/stop clears the tie. */
    if(!tied) {
        base=fm_minf((float)v->sound.cutoff_hz,16000.0f);
        top=fm_minf(16000.0f,base+v->sound.envelope*45.0f);
        v->cutoff=top;v->filter_left=fraction(frames,80);
        if(v->filter_left<1323u)v->filter_left=1323u; /* Studio minimum30ms */
        v->filter_total=v->filter_left;
        v->cutoff_ratio=fm_exp2f(fm_log2f(base/top)/(float)v->filter_left);
    }
    v->control_phase=0;
}
int bb_telegram_key(bb_telegram *v,unsigned note,unsigned velocity,int legato,
                    uint32_t frames) {
    bb_note n={0};
    if(!v || note>127 || !velocity || velocity>127 || !frames || frames==UINT32_MAX)return 0;
    n.note=(uint8_t)note;n.gate=1;n.accent=(uint8_t)(velocity>=100);
    /* Keyboard overlap, not the preceding sequencer's outgoing slide flag,
     * determines whether to retrigger. Preserve global glide for separate keys. */
    v->slide_out=(uint8_t)(legato && v->gate_left!=0);
    bb_telegram_step(v,n,frames);
    v->gate_left=UINT32_MAX;v->slide_out=0;
    return 1;
}
void bb_telegram_render(bb_telegram *v,float *out,uint32_t frames) {
    uint32_t i;
    for(i=0;i<frames;i++) {
        float dt,saw,square,sub,x,fc,g,v1,v2,v3,c;
        if(!v->gate_left)v->amp_target=0;
        if(v->gate_left && v->gate_left!=UINT32_MAX)v->gate_left--;
        c=v->amp_target>v->amp?v->attack_c:v->release_c;
        v->amp=v->amp_target+(v->amp-v->amp_target)*c;
        if(!v->amp_target && v->amp<1e-9f)v->amp=0;
        if(v->glide_left){v->pitch*=v->pitch_ratio;if(!--v->glide_left)v->pitch=v->target_pitch;}
        dt=v->pitch/TG_SR;
        saw=2*v->phase-1-blep(v->phase,dt);square=pulse(v->phase,dt);sub=pulse(v->sub_phase,dt*0.5f);
        v->phase+=dt;if(v->phase>=1)v->phase-=1;
        v->sub_phase+=dt*0.5f;if(v->sub_phase>=1)v->sub_phase-=1;
        v->lfo_phase+=(float)v->sound.lfo_rate_x100/(100.0f*TG_SR);if(v->lfo_phase>=1)v->lfo_phase-=1;
        v->wave_mix[0]=v->sound.square*0.01f+(v->wave_mix[0]-v->sound.square*0.01f)*v->mix_c;
        v->wave_mix[1]=v->sound.saw*0.01f+(v->wave_mix[1]-v->sound.saw*0.01f)*v->mix_c;
        v->wave_mix[2]=v->sound.sub*0.01f+(v->wave_mix[2]-v->sound.sub*0.01f)*v->mix_c;
        x=square*v->wave_mix[0]+saw*v->wave_mix[1]+sub*v->wave_mix[2];
        /* The persistent control phase makes coefficients caller-block
         * independent. This slow modulation path is NOT audio-rate filter FM. */
        if(!v->control_phase) {
            fc=fm_clampf(v->cutoff+14.0f*v->sound.modulation*fm_sinf(FM_TWO_PI*v->lfo_phase),20,16000);
            g=fm_tanf(FM_PI*fc/TG_SR);
            v->a1=1.0f/(1.0f+g*(g+v->damping));v->a2=g*v->a1;v->a3=g*v->a2;
        }
        v->control_phase=(uint8_t)((v->control_phase+1u)&7u);
        v3=x-v->ic2;v1=v->a1*v->ic1+v->a2*v3;v2=v->ic2+v->a2*v->ic1+v->a3*v3;
        v->ic1=2*v1-v->ic1;v->ic2=2*v2-v->ic2;
        out[i]=v2*v->amp;
        if(v->filter_left) {
            v->cutoff*=v->cutoff_ratio;
            if(!--v->filter_left)v->cutoff=fm_minf((float)v->sound.cutoff_hz,16000);
        }
    }
}
