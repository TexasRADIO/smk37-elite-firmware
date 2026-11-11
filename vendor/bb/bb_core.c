#include "bb_core.h"
#include <string.h>
#include "bb_drum_sound.inc"

/* Original Studio recipes, not FM-1-calibrated DSP coefficients.
   Source: app/studio/researchedPatches.json, dillo-sound-1..8.
   Wave: 0 square, 1 saw. No pattern, transport or tempo values in a sound. */
const bb_named_dillo bb_dillo_bank[BB_DILLO_PATCHES] = {
    {"Silver Pulse", {600,800,0,45,30,65,0,65,3,0}},
    {"Liquid Wire", {400,1300,1,75,48,85,0,65,3,0}},
    {"Dry Jack", {320,500,0,35,15,75,0,65,3,0}},
    {"Open Circuit", {1700,1000,1,55,35,60,0,65,3,0}},
    {"Rubber Acid", {260,1500,0,88,42,90,10,65,3,0}},
    {"Long Fuse", {500,1200,1,70,85,75,15,65,3,0}},
    {"Hot Valve", {700,900,1,60,28,80,65,65,3,0}},
    {"Hollow Dub", {220,700,0,30,65,50,5,65,3,0}}
};
/* Original Studio sound-only recipes from synthPresets.ts; legacy mono mode.
 * Attack/release/glide defaults from instrument.ts; not circuit calibration. */
const bb_named_telegram bb_telegram_bank[BB_TELEGRAM_PATCHES] = {
    {"Big Beat Line", {2200,200,65,30,28,36,0,0,500,6,18,80}},
    {"New Wave", {4100,300,72,44,12,48,8,0,640,6,18,80}},
    {"Pulsing Bass", {1280,450,78,12,68,54,0,0,310,6,18,80}},
    {"Octave Runner", {3600,500,64,58,30,62,5,1,700,6,18,80}},
    {"Dark Sequence", {980,700,42,38,72,31,15,1,240,6,18,80}},
    {"Broadcast Signal", {5900,600,82,24,0,69,22,0,920,6,18,80}},
    {"Detroit Telegram", {3900,550,48,72,14,44,14,0,760,6,18,80}},
    {"Miami Telegram", {1800,250,65,22,58,42,3,0,340,6,18,80}}
};
int bb_init(bb_core *c, uint32_t rate) {
    static const uint16_t effects[BB_FX_PARAMETER_COUNT]={360,55,50,25,0,50,7,55,55,35,0,0,0,0};
    unsigned i;
    if (!c || rate < 8000 || rate > 192000) return 0;
    memset(c,0,sizeof(*c));
    c->sample_rate=rate; c->bpm=120; c->page=BB_DRUMS;
    c->swing=c->active_swing=50;
    for(i=0;i<BB_MIX_CHANNELS;i++)c->band_level[i]=127;
    for(i=0;i<BB_MACHINES;i++) {
        c->drums[i].length=16; c->drums[i].level=127;
        c->drums[i].enabled=(uint8_t)(i<2); c->drums[i].kit=0;
        bb_drum_sound_recall(&c->drums[i].sound,0);c->drums[i].sound_revision=1;
    }
    for(i=0;i<BB_SYNTHS;i++) c->synths[i].length=16;
    c->dillo=bb_dillo_bank[0].sound;
    c->telegram=bb_telegram_bank[0].sound;
    memcpy(c->effects.value,effects,sizeof effects);
    return 1;
}
int bb_fx_parameter_limits(unsigned parameter,unsigned *lo,unsigned *hi) {
    unsigned minimum=0,maximum=100;
    if(parameter>=BB_FX_PARAMETER_COUNT || !lo || !hi)return 0;
    if(parameter==BB_FX_ECHO_MS){minimum=10;maximum=700;}
    else if(parameter==BB_FX_ECHO_FEEDBACK)maximum=90;
    else if(parameter==BB_FX_ECHO_HEADS)maximum=7;
    else if(parameter>=BB_FX_DILLO_ECHO && parameter<=BB_FX_TELEGRAM_SPRING)maximum=127;
    *lo=minimum;*hi=maximum;return 1;
}
unsigned bb_fx_parameter_get(const bb_core *c,unsigned parameter) {
    return c && parameter<BB_FX_PARAMETER_COUNT?c->effects.value[parameter]:0;
}
int bb_fx_parameter_set(bb_core *c,unsigned parameter,unsigned value) {
    unsigned lo,hi;
    if(!c || !bb_fx_parameter_limits(parameter,&lo,&hi) || value<lo || value>hi)return 0;
    c->effects.value[parameter]=(uint16_t)value;return 1;
}
int bb_recall_drum(bb_core *c,unsigned machine,unsigned kit) {
    if(!c || machine>=BB_MACHINES || kit>=BB_DRUM_KITS)return 0;
    if(c->drums[machine].kit==kit && !c->drums[machine].sound.edited)return 1;
    bb_drum_sound_recall(&c->drums[machine].sound,kit);
    c->drums[machine].kit=(uint8_t)kit;c->drums[machine].sound_revision++;
    return 1;
}
int bb_set_drum_parameter(bb_core *c,unsigned machine,unsigned track,unsigned parameter,unsigned value) {
    unsigned old;
    if(!c || machine>=BB_MACHINES)return 0;
    old=bb_drum_parameter_get(&c->drums[machine].sound,track,parameter);
    if(!bb_drum_parameter_set(&c->drums[machine].sound,track,parameter,value))return 0;
    if(old!=value)c->drums[machine].sound_revision++;
    return 1;
}
int bb_set_tempo(bb_core *c, unsigned bpm) {
    if(!c || bpm<30 || bpm>300) return 0;
    c->bpm=(uint16_t)bpm; return 1;
}
int bb_set_swing(bb_core *c,unsigned percent) {
    if(!c || percent<50 || percent>75)return 0;
    c->swing=(uint8_t)percent;return 1;
}
int bb_set_length(bb_core *c,unsigned m,unsigned n) {
    if(!c || m>=BB_MACHINES || n<1 || n>BB_STEPS) return 0;
    c->drums[m].length=(uint8_t)n;
    c->drums[m].next=(uint8_t)(c->drums[m].next%n); return 1;
}
int bb_set_enabled(bb_core *c,unsigned m,int on) {
    if(!c || m>=BB_MACHINES) return 0;
    c->drums[m].enabled=(uint8_t)(on!=0); return 1;
}
int bb_set_synth_length(bb_core *c,unsigned synth,unsigned length) {
    if(!c || synth>=BB_SYNTHS || !length || length>BB_STEPS)return 0;
    c->synths[synth].length=(uint8_t)length;
    c->synths[synth].next=(uint8_t)(c->synths[synth].next%length);return 1;
}
int bb_synth_parameter_limits(unsigned synth,unsigned parameter,unsigned *lo,unsigned *hi) {
    unsigned minimum=0,maximum=100;
    if(synth>=BB_SYNTHS || !lo || !hi)return 0;
    switch(parameter) {
    /* Both native filters currently stop at16k: no dead20k knob region. */
    case BB_PARAM_CUTOFF:minimum=20;maximum=16000;break;
    case BB_PARAM_RESONANCE:maximum=2000;break;
    case BB_PARAM_ENVELOPE:break;
    case BB_PARAM_DECAY:case BB_PARAM_ACCENT:case BB_PARAM_DRIVE:if(synth)return 0;break;
    case BB_PARAM_WAVE:if(synth)return 0;maximum=1;break;
    case BB_PARAM_SLIDE_MS:if(synth)return 0;minimum=1;maximum=255;break;
    case BB_PARAM_SQUARE:case BB_PARAM_SAW:case BB_PARAM_SUB:case BB_PARAM_MODULATION:if(!synth)return 0;break;
    case BB_PARAM_GLIDE:if(!synth)return 0;maximum=1;break;
    case BB_PARAM_LFO_RATE:if(!synth)return 0;maximum=3000;break;
    default:return 0;
    }
    *lo=minimum;*hi=maximum;return 1;
}
unsigned bb_synth_parameter_get(const bb_core *c,unsigned synth,unsigned parameter) {
    unsigned lo,hi;
    if(!c || !bb_synth_parameter_limits(synth,parameter,&lo,&hi))return 0;
    if(!synth) {
        const bb_dillo_patch *p=&c->dillo;
        switch(parameter) {
        case BB_PARAM_CUTOFF:return p->cutoff_hz;case BB_PARAM_RESONANCE:return p->resonance_x100;
        case BB_PARAM_ENVELOPE:return p->envelope;case BB_PARAM_DECAY:return p->decay;
        case BB_PARAM_ACCENT:return p->accent;case BB_PARAM_DRIVE:return p->drive;
        case BB_PARAM_WAVE:return p->wave;case BB_PARAM_SLIDE_MS:return p->slide_ms;
        default:return 0;
        }
    } else {
        const bb_telegram_patch *p=&c->telegram;
        switch(parameter) {
        case BB_PARAM_CUTOFF:return p->cutoff_hz;case BB_PARAM_RESONANCE:return p->resonance_x100;
        case BB_PARAM_ENVELOPE:return p->envelope;case BB_PARAM_SQUARE:return p->square;
        case BB_PARAM_SAW:return p->saw;case BB_PARAM_SUB:return p->sub;
        case BB_PARAM_MODULATION:return p->modulation;case BB_PARAM_GLIDE:return p->glide;
        case BB_PARAM_LFO_RATE:return p->lfo_rate_x100;default:return 0;
        }
    }
}
int bb_synth_parameter_set(bb_core *c,unsigned synth,unsigned parameter,unsigned value) {
    unsigned lo,hi;
    if(!c || !bb_synth_parameter_limits(synth,parameter,&lo,&hi) || value<lo || value>hi)return 0;
    if(!synth) {
        bb_dillo_patch *p=&c->dillo;
        switch(parameter) {
        case BB_PARAM_CUTOFF:p->cutoff_hz=(uint16_t)value;break;case BB_PARAM_RESONANCE:p->resonance_x100=(uint16_t)value;break;
        case BB_PARAM_ENVELOPE:p->envelope=(uint8_t)value;break;case BB_PARAM_DECAY:p->decay=(uint8_t)value;break;
        case BB_PARAM_ACCENT:p->accent=(uint8_t)value;break;case BB_PARAM_DRIVE:p->drive=(uint8_t)value;break;
        case BB_PARAM_WAVE:p->wave=(uint8_t)value;break;case BB_PARAM_SLIDE_MS:p->slide_ms=(uint8_t)value;break;
        default:return 0;
        }
    } else {
        bb_telegram_patch *p=&c->telegram;
        switch(parameter) {
        case BB_PARAM_CUTOFF:p->cutoff_hz=(uint16_t)value;break;case BB_PARAM_RESONANCE:p->resonance_x100=(uint16_t)value;break;
        case BB_PARAM_ENVELOPE:p->envelope=(uint8_t)value;break;case BB_PARAM_SQUARE:p->square=(uint8_t)value;break;
        case BB_PARAM_SAW:p->saw=(uint8_t)value;break;case BB_PARAM_SUB:p->sub=(uint8_t)value;break;
        case BB_PARAM_MODULATION:p->modulation=(uint8_t)value;break;case BB_PARAM_GLIDE:p->glide=(uint8_t)value;break;
        case BB_PARAM_LFO_RATE:p->lfo_rate_x100=(uint16_t)value;break;default:return 0;
        }
    }
    return 1;
}
int bb_select_drum(bb_core *c,unsigned m,unsigned v) {
    if(!c || m>=BB_MACHINES || v>=BB_VOICES) return 0;
    c->selected_machine=(uint8_t)m; c->selected_voice=(uint8_t)v;
    return 1;
}
int bb_set_page(bb_core *c,bb_page page) {
    if(!c || page<BB_DRUMS || page>BB_TELEGRAM) return 0;
    c->page=page; c->edit=BB_GATE; return 1;
}
int bb_set_edit(bb_core *c,bb_edit edit) {
    if(!c || edit<BB_GATE || edit>BB_SLIDE || (c->page==BB_DRUMS && edit==BB_SLIDE)) return 0;
    c->edit=edit; return 1;
}
int bb_press_step(bb_core *c,unsigned step,unsigned note) {
    if(!c || step>=BB_STEPS || note>127) return 0;
    if(c->page==BB_DRUMS) {
        bb_machine *m=&c->drums[c->selected_machine];
        uint16_t bit=(uint16_t)(1u<<c->selected_voice);
        if(step>=m->length) return 0;
        if(c->edit==BB_ACCENT) {
            if(!(m->steps[step].hits&bit)) return 0;
            m->steps[step].accents^=bit;
        } else {
            m->steps[step].hits^=bit;
            m->steps[step].accents&=m->steps[step].hits;
        }
    } else {
        bb_sequence *s=&c->synths[c->page==BB_DILLO?0:1];
        bb_note *n=&s->notes[step];
        if(step>=s->length) return 0;
        if(c->edit==BB_ACCENT) { if(!n->gate)return 0; n->accent^=1; }
        else if(c->edit==BB_SLIDE) { if(!n->gate)return 0; n->slide^=1; }
        else { n->gate^=1; n->note=(uint8_t)note; if(!n->gate)n->accent=n->slide=0; }
    }
    return 1;
}
int bb_recall_dillo(bb_core *c,unsigned patch) {
    if(!c || patch>=BB_DILLO_PATCHES) return 0;
    c->dillo=bb_dillo_bank[patch].sound; return 1;
}
int bb_telegram_patch_valid(const bb_telegram_patch *p) {
    return p && p->cutoff_hz>=20 && p->cutoff_hz<=20000 && p->resonance_x100<=2000 &&
       p->square<=100 && p->saw<=100 && p->sub<=100 && p->envelope<=100 && p->modulation<=100 && p->glide<=1 &&
       p->lfo_rate_x100<=3000 && p->attack_ms>=1 && p->attack_ms<=5000 &&
       p->release_ms>=1 && p->release_ms<=5000 && p->glide_ms>=1 && p->glide_ms<=5000;
}
int bb_recall_telegram(bb_core *c,const bb_telegram_patch *p) {
    if(!c || !bb_telegram_patch_valid(p))return 0;
    c->telegram=*p; return 1;
}
void bb_start(bb_core *c) {
    unsigned i;
    if(!c)return;
    c->running=1; c->pending_tick=1; c->phase=0; c->frame=0;c->ticks=0;
    c->active_swing=c->swing;c->swing_odd=0;
    for(i=0;i<BB_MACHINES;i++)c->drums[i].next=0;
    for(i=0;i<BB_SYNTHS;i++)c->synths[i].next=0;
}
void bb_stop(bb_core *c) { if(c)c->running=0; }
static void tick(bb_core *c,bb_hit_fn hit,bb_note_fn note,void *ctx) {
    unsigned m,v;
    c->ticks++;
    for(m=0;m<BB_MACHINES;m++) {
        bb_machine *d=&c->drums[m]; const bb_drum_step *s=&d->steps[d->next];
        if(d->enabled && !d->muted && hit) for(v=0;v<BB_VOICES;v++) if(s->hits&(1u<<v)) {
            unsigned vel=(s->accents&(1u<<v))?127:88;
            /* Channel volume belongs after synthesis, not in the strike. */
            bb_hit e={c->frame,(uint8_t)m,d->kit,(uint8_t)v,d->next,(uint8_t)vel};
            hit(ctx,&e);
        }
        /* Muting/disabling never drifts a machine away from the shared clock. */
        d->next=(uint8_t)((d->next+1)%d->length);
    }
    for(m=0;m<BB_SYNTHS;m++) {
        bb_sequence *s=&c->synths[m];
        /* Emit rests too: the audio adapter must release its previous note. */
        if(note) {
            bb_note_event e={c->frame,(uint8_t)m,s->next,s->notes[s->next]};
            if(s->muted) e.note.gate=e.note.accent=e.note.slide=0;
            note(ctx,&e);
        }
        s->next=(uint8_t)((s->next+1)%s->length);
    }
}
void bb_process_render_hook(bb_core *c,uint32_t frames,bb_hit_fn hit,bb_note_fn note,
                       bb_render_fn render,bb_before_tick_fn before,void *ctx) {
    uint32_t denom,increment;
    if(!c || !frames)return;
    if(!c->running) { if(render)render(ctx,frames); return; }
    while(frames) {
        uint32_t span;
        if(c->pending_tick) {
            if(!c->swing_odd)c->active_swing=c->swing;
            c->pending_tick=0;
            if(before)before(ctx,c);
            tick(c,hit,note,ctx);
        }
        /* A part-launch hook can change tempo at this exact tick. Reading it
         * here preserves the same schedule for every caller block size. */
        increment=(uint32_t)c->bpm*400u;
        /* Pair length is invariant: 2*swing + 2*(100-swing) = 200.
           Denominator <= 1,728,000,000 at 192kHz/75%; fits uint32_t. */
        denom=c->sample_rate*60u*2u*(c->swing_odd?100u-c->active_swing:c->active_swing);
        /* Integer ceiling preserves the original sample-by-sample clock,
           including its deferred tick when a boundary ends the caller's block.
           Valid rates/tempos keep span*increment <= denom+increment-1. */
        span=(denom-c->phase+increment-1u)/increment;
        if(span>frames)span=frames;
        if(render)render(ctx,span);
        c->phase+=span*increment;
        if(c->phase>=denom) { c->phase-=denom; c->pending_tick=1;c->swing_odd^=1; }
        c->frame+=span;
        frames-=span;
    }
}
void bb_process_render(bb_core *c,uint32_t frames,bb_hit_fn hit,bb_note_fn note,
                       bb_render_fn render,void *ctx) {
    bb_process_render_hook(c,frames,hit,note,render,NULL,ctx);
}
void bb_process(bb_core *c,uint32_t frames,bb_hit_fn hit,bb_note_fn note,void *ctx) {
    bb_process_render(c,frames,hit,note,NULL,ctx);
}
