/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat adapter. Filter-envelope coefficient mapping below follows the
 * retained X0X bass303.c (Open303 by Robin Schmidt, MIT); see that source's
 * copyright/provenance and documented approximations. References stay intact. */
#include "bb_dillo.h"
#include "fastmath.h"
#include <string.h>
static int percent(unsigned n){return (int)((n*127u+50u)/100u);}
static void dillo_powers(float *pw,float c) {
    unsigned i;pw[0]=1;for(i=1;i<=BASS303_CTRL;i++)pw[i]=pw[i-1]*c;
}
static float dillo_rc(float ms){return fm_expf(-1000.0f/(ms*BASS303_SR));}
static void dillo_cutoff(bb_dillo *d,unsigned note) {
    bass303_t *b=&d->voice;
    float c,e,slo,shi;
    /* Keep Hz recipes below the stock reference's 314 Hz pot minimum distinct.
     * Tracking is musical semitones, not an assertion of Devil Fish circuitry. */
    float hz=(float)d->sound.cutoff_hz*fm_exp2f(((float)note-36.0f)*(float)d->sound.tracking/1200.0f);
    if(d->live_mod)hz*=fm_exp2f(d->live_mod*(2.0f/127.0f));
    b->cutoff=fm_clampf(hz,20.0f,16000.0f);
    c=fm_log2f(b->cutoff*(1.0f/313.8152786f))*(1.0f/2.931683907f);
    e=(float)b->pot[BASS303_ENVMOD]*(1.0f/127.0f);
    slo=3.773996325f*e+0.736965594f;shi=4.194548788f*e+0.864344901f;
    b->env_scaler=(1.0f-c)*slo+c*shi;b->env_offset=0.048292931f*c+0.294391201f;
}
void bb_dillo_init(bb_dillo *d) {
    memset(d,0,sizeof *d);bass303_init(&d->voice);d->release_in=UINT32_MAX;d->last_note=36;
}
int bb_dillo_sound(bb_dillo *d,const bb_dillo_patch *p) {
    bass303_t *b;unsigned i;int first,decay,slide,attack,cutoff;
    if(!d || !p || p->cutoff_hz<20 || p->cutoff_hz>16000 || p->resonance_x100>2000 ||
       p->wave>1 || p->envelope>100 || p->decay>100 || p->accent>100 || p->drive>100 ||
       !p->slide_ms || !p->attack_ms || p->tracking>100)return 0;
    /* Only patch-dependent coefficients are cached. Note-dependent envelope
     * tables still belong to note_on, and cutoff tracking still runs per note.
     * Compute flags before copying: callers may pass &d->sound. */
    first=!d->has_sound || p==&d->sound;
    decay=first || p->decay!=d->sound.decay;
    slide=first || p->slide_ms!=d->sound.slide_ms;
    attack=first || p->attack_ms!=d->sound.attack_ms;
    cutoff=first || p->cutoff_hz!=d->sound.cutoff_hz ||
        p->tracking!=d->sound.tracking || p->envelope!=d->sound.envelope;
    d->sound=*p;d->has_sound=1;b=&d->voice;
    /* Studio Q and TeeBeeFilter resonance are different quantities. This
     * bounded initial mapping is for audition/calibration, not equivalence. */
    {
        const int params[6]={BASS303_RESO,BASS303_ENVMOD,BASS303_ACCENT,
            BASS303_WAVE,BASS303_DRIVE,BASS303_DRVTYPE};
        const int values[6]={(int)((p->resonance_x100*127u+1000u)/2000u),
            percent(p->envelope),percent(p->accent),p->wave?0:1,
            percent(p->drive),BASS303_DRV_SOFT};
        for(i=0;i<6;i++)if(bass303_get(b,params[i])!=values[i])
            bass303_set(b,params[i],values[i]);
    }
    /* Studio decay / 100 + .03 seconds; no silent clipping to stock 200 ms. */
    if(decay)b->decay_c=dillo_rc((float)p->decay*10.0f+30.0f);
    if(slide){b->slew_c=dillo_rc(0.2f*(float)p->slide_ms);dillo_powers(b->slew_pw,b->slew_c);}
    /* Normal-attack follower, not an invented amp-attack control. */
    if(attack) {
        b->rc1_c=dillo_rc((float)p->attack_ms);dillo_powers(b->rc1_pw,b->rc1_c);
        b->g1[0]=0;
        for(i=1;i<=BASS303_CTRL;i++)b->g1[i]=b->rc1_c*b->g1[i-1]+(1.0f-b->rc1_c)*b->env_pw[i];
    }
    if(cutoff)dillo_cutoff(d,d->last_note);
    return 1;
}
void bb_dillo_expression(bb_dillo *d, int bend, unsigned mod)
{
    if (bend < -512) bend = -512;
    if (bend > 511) bend = 511;
    if (mod > 127) mod = 127;
    if (bend != d->live_bend) {
        d->live_bend = (int16_t)bend;
        d->voice.pitch_ratio = bend ? fm_exp2f((float)bend *
            (bend < 0 ? 1.0f / (6.0f * 512.0f) : 1.0f / (6.0f * 511.0f))) : 1.0f;
    }
    if (mod != d->live_mod) {
        d->live_mod = (uint8_t)mod;
        if (d->has_sound) dillo_cutoff(d, d->last_note);
    }
}

void bb_dillo_stop(bb_dillo *d) {
    bass303_note_off(&d->voice);d->slide_out=0;d->release_in=UINT32_MAX;
}
int bb_dillo_key(bb_dillo *d,unsigned note,unsigned velocity,int legato) {
    if(!d || note>127 || !velocity || velocity>127)return 0;
    if(d->has_sound)dillo_cutoff(d,note);
    d->last_note=(uint8_t)note;
    bass303_note_on(&d->voice,(int)note,velocity>=100,legato && d->voice.gate);
    d->slide_out=0;d->release_in=UINT32_MAX;
    return 1;
}
void bb_dillo_step(bb_dillo *d,bb_note n,uint32_t frames,int next_gate) {
    int incoming=d->slide_out && d->voice.gate;
    if(!n.gate || !frames){bb_dillo_stop(d);return;}
    bb_dillo_expression(d,0,0);
    if(d->has_sound)dillo_cutoff(d,n.note);
    d->last_note=n.note;
    bass303_note_on(&d->voice,n.note,n.accent,incoming);
    d->slide_out=(uint8_t)(n.slide && next_gate);
    /* Split multiply so a long externally supplied duration cannot overflow. */
    d->release_in=d->slide_out?UINT32_MAX:(frames/100u)*65u+(frames%100u)*65u/100u;
    if(!d->release_in)d->release_in=1;
}
void bb_dillo_render(bb_dillo *d,float *out,uint32_t frames) {
    while(frames) {
        uint32_t n=frames>BASS303_MAX_BLOCK?BASS303_MAX_BLOCK:frames;
        if(d->release_in==0){bass303_note_off(&d->voice);d->release_in=UINT32_MAX;}
        if(n>d->release_in)n=d->release_in;
        bass303_render(&d->voice,out,(int)n);out+=n;frames-=n;
        if(d->release_in!=UINT32_MAX)d->release_in-=n;
    }
}
