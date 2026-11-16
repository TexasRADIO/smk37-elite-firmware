/* SPDX-License-Identifier: GPL-3.0-only */
#include "store101.h"
#include <string.h>

enum { PAGE = 256, USABLE = TG_STORE_SECTOR - PAGE, HEADER = 32,
       PATCH_BYTES = TG_NAME_BYTES + TG_PARAMETERS + 7,
       PHRASE_BYTES = TG_NAME_BYTES + 1 + TG_STEPS,
       OLD_PAYLOAD = 16 + TG_USER_SLOTS * (PATCH_BYTES + PHRASE_BYTES),
       V3_PAYLOAD = OLD_PAYLOAD + TG_LAYOUT_BYTES,
       V4_PAYLOAD = V3_PAYLOAD + 2 * TG_USER_SLOTS,
       V5_PAYLOAD = V4_PAYLOAD + TG_USER_SLOTS,
       V6_PAYLOAD = V5_PAYLOAD + TG_USER_SLOTS,
       V7_PAYLOAD = V6_PAYLOAD + TG_USER_SLOTS,
       V8_PAYLOAD = V7_PAYLOAD + TG_USER_SLOTS,
       V9_PAYLOAD = V8_PAYLOAD + TG_USER_SLOTS,
       V10_PAYLOAD = V9_PAYLOAD + (TG_LAYOUT_PROFILES - 1) * TG_LAYOUT_BYTES + 1,
       FILTER_ENV_BYTES = TG_ADSR_STAGES + 1,
       V11_PAYLOAD = V10_PAYLOAD + TG_USER_SLOTS * FILTER_ENV_BYTES,
       V13_PAYLOAD = V11_PAYLOAD + TG_USER_SLOTS * 2,
       PAYLOAD = V13_PAYLOAD + TG_USER_SLOTS,
       PACKED_PAYLOAD = PAYLOAD - TG_USER_SLOTS * (TG_PATCH_WIRE_BYTES - TG_PATCH_STORE_BYTES) };
enum { TAIL_HEADER = 12 };
_Static_assert(HEADER + PACKED_PAYLOAD + TAIL_HEADER + TG_STORE_TAIL_MAX <= USABLE * 4,
               "tail exceeds journal bank");
_Static_assert(sizeof(tg_layout) == TG_LAYOUT_BYTES, "layout must have no padding");
static const uint8_t magic[8] = {'T','G','1','0','1','S','T',0};
#define COMMIT UINT32_C(0x314b4f54)

/* Every sector's final page stays erased: the retained updater examines
 * offset 0xF00 for records and must never mistake sound data for an update. */
static unsigned physical(unsigned logical) { return logical / USABLE * TG_STORE_SECTOR + logical % USABLE; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void wr32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;i++) { p[i]=(uint8_t)n; n>>=8; } }
static uint32_t crc(const uint8_t *image, unsigned offset, unsigned n)
{
    uint32_t c = UINT32_MAX;
    while (n--) {
        c ^= image[physical(offset++)];
        for (unsigned b=0;b<8;b++) c=(c>>1)^((c&1)?UINT32_C(0xedb88320):0);
    }
    return ~c;
}
static int present(const uint8_t *map, unsigned slot) { return (map[slot/8]>>(slot%8))&1; }
static int valid_name(const char *s)
{
    if (!s[0]) return 0;
    for (unsigned i=0;i<TG_NAME_BYTES;i++) {
        if (!s[i]) return 1;
        if ((unsigned char)s[i]<32 || (unsigned char)s[i]>126) return 0;
    }
    return 0;
}
static int valid_filter_env(const tg_patch *p)
{
    if (p->filter_env_mode > TG_FILTER_ENV_CLOSE) return 0;
    for (unsigned i = 0; i < TG_ADSR_STAGES; i++)
        if (p->filter_adsr[i] > 128) return 0;
    return 1;
}
static int valid_patch(const tg_patch *p)
{
    for (unsigned i=0;i<TG_PARAMETERS;i++) if (p->value[i]>127) return 0;
    return p->accent_tone <= 127 && p->accent_level <= 127 && valid_filter_env(p) && p->drive<=127 && p->trigger<=TG_LFO_TRIG && p->pwm < TG_PWM_SOURCE_COUNT && p->sub_mode<=2 && p->amp_gate<=1 &&
           p->filter_idle_hold<=1 && p->lfo_retrigger<=1 && p->filter_low<=1 && p->env_wide<=1 && p->osc_octave>=-2 && p->osc_octave<=2 && p->key_octave>=-2 && p->key_octave<=2 &&
           p->glide_mode<=TG_GLIDE_ON && p->range<=TG_RANGE_2 && p->lfo_wave<=TG_LFO_NOISE;
}
static int valid_phrase_gate(unsigned gate)
{
    return !gate || (gate >= 5 && gate <= 100);
}
static int valid_phrase(const tg_saved_phrase *p)
{
    if (!p->length || p->length>TG_STEPS || !valid_phrase_gate(p->gate)) return 0;
    for (unsigned i=0;i<TG_STEPS;i++) if (p->steps[i]==TG_TIE) return 0;
    return 1;
}
const uint8_t tg_default_knob_map[2][8] = {
    {TG_CONTROL_LEVEL, TG_CUTOFF, TG_RESONANCE, TG_ENV, TG_WIDTH, TG_DECAY, TG_LFO_RATE, TG_GLIDE},
    {TG_CONTROL_LEVEL, TG_TRACK, TG_PWM, TG_FILTER_LFO, TG_VIBRATO, TG_CONTROL_OSC_OCTAVE, TG_CONTROL_TEMPO, TG_GLIDE},
};

void tg_layout_default(tg_layout *p)
{
    static const uint8_t faders[4][4] = {
        {TG_SAW,TG_PULSE,TG_SUB,TG_NOISE}, {TG_CUTOFF,TG_RESONANCE,TG_ENV,TG_TRACK},
        {TG_ATTACK,TG_DECAY,TG_SUSTAIN,TG_RELEASE}, {TG_LFO_RATE,TG_WIDTH,TG_PWM,TG_FILTER_LFO},
    };
    memcpy(p->knobs, tg_default_knob_map, sizeof p->knobs); memcpy(p->faders,faders,sizeof faders);
    for(unsigned i=0;i<TG_PADS;i++) p->pads[i]=(uint8_t)(48+i);
    p->brightness=72;
}
static int valid_layout(const tg_layout *p, unsigned control_count)
{
    for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(p->knobs[b][i]>=control_count) return 0;
    for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(p->faders[b][i]>=control_count) return 0;
    for(unsigned i=0;i<TG_PADS;i++) if(p->pads[i]<12 || p->pads[i]>108) return 0;
    return p->brightness<=127;
}
void tg_library_init(tg_library *l)
{
    memset(l, 0, sizeof *l);
    for (unsigned i = 0; i < TG_LAYOUT_PROFILES; i++) tg_layout_default(&l->layouts[i]);
}
int tg_library_put_patch(tg_library *l,unsigned slot,const char *name,const tg_controls *c)
{
    if (slot>=TG_USER_SLOTS || !valid_name(name) || !valid_patch(&c->patch)) return TG_STORE_INVALID;
    tg_saved_patch *p=&l->patches[slot];
    memset(p,0,sizeof *p);
    for (unsigned i=0;name[i];i++) p->name[i]=name[i];
    p->patch=c->patch;
    l->patches_present[slot/8]|=(uint8_t)(1u<<(slot%8));
    return 0;
}
int tg_library_put_phrase(tg_library *l,unsigned slot,const char *name,const tg_controls *c)
{
    if (slot>=TG_USER_SLOTS || !valid_name(name)) return TG_STORE_INVALID;
    tg_saved_phrase p;
    memset(&p,0,sizeof p);
    p.length=(uint8_t)tg_pattern_length(c);
    p.gate=c->phrase_gate;
    memcpy(p.steps,c->steps,TG_STEPS);
    if (!valid_phrase(&p)) return TG_STORE_INVALID;
    for (unsigned i=0;name[i];i++) p.name[i]=name[i];
    l->phrases[slot]=p;
    l->phrases_present[slot/8]|=(uint8_t)(1u<<(slot%8));
    return 0;
}
int tg_library_get_patch(const tg_library *l,unsigned slot,tg_controls *c)
{
    if (slot>=TG_USER_SLOTS || !present(l->patches_present,slot)) return TG_STORE_EMPTY;
    if (!valid_name(l->patches[slot].name) || !valid_patch(&l->patches[slot].patch)) return TG_STORE_INVALID;
    c->patch=l->patches[slot].patch;
    return 0;
}
int tg_library_find_phrase(const tg_library *l, unsigned slot, const tg_saved_phrase **phrase)
{
    if (slot>=TG_USER_SLOTS || !present(l->phrases_present,slot)) return TG_STORE_EMPTY;
    const tg_saved_phrase *p=&l->phrases[slot];
    if (!valid_name(p->name) || !valid_phrase(p)) return TG_STORE_INVALID;
    *phrase = p;
    return 0;
}
int tg_library_get_phrase(const tg_library *l,unsigned slot,tg_controls *c)
{
    const tg_saved_phrase *p;
    int rc = tg_library_find_phrase(l, slot, &p);
    if (rc) return rc;
    memcpy(c->steps,p->steps,TG_STEPS);
    c->length=p->length;
    c->phrase_gate=p->gate;
    return 0;
}

/* Explicit byte encoding: no ABI padding, endian assumptions or pointers. */
static void put(uint8_t *im,unsigned *at,const void *src,unsigned n)
{ const uint8_t *s=src; while(n--) im[physical((*at)++)]=*s++; }
static void get(const uint8_t *im,unsigned *at,void *dst,unsigned n)
{ uint8_t *d=dst; while(n--) *d++=im[physical((*at)++)]; }
static void pack_patch(uint8_t out[TG_PARAMETERS+7],const tg_patch *p)
{
    memcpy(out,p->value,TG_PARAMETERS);
    out[TG_PARAMETERS]=p->trigger; out[TG_PARAMETERS+1]=p->pwm; out[TG_PARAMETERS+2]=p->sub_mode;
    out[TG_PARAMETERS+3]=p->amp_gate; out[TG_PARAMETERS+4]=p->glide_mode;
    out[TG_PARAMETERS+5]=p->range; out[TG_PARAMETERS+6]=p->lfo_wave;
}
static void unpack_patch(tg_patch *p,const uint8_t in[TG_PARAMETERS+7])
{
    memcpy(p->value,in,TG_PARAMETERS);
    p->trigger=in[TG_PARAMETERS]; p->pwm=in[TG_PARAMETERS+1]; p->sub_mode=in[TG_PARAMETERS+2];
    p->amp_gate=in[TG_PARAMETERS+3]; p->glide_mode=in[TG_PARAMETERS+4];
    p->range=in[TG_PARAMETERS+5]; p->lfo_wave=in[TG_PARAMETERS+6];
}
void tg_patch_encode(uint8_t out[TG_PATCH_WIRE_BYTES], const tg_patch *p)
{
    pack_patch(out, p);
    uint8_t *tail = out + TG_PARAMETERS + 7;
    *tail++ = p->env_wide;
    *tail++ = (uint8_t)(p->osc_octave + 2);
    *tail++ = (uint8_t)(p->key_octave + 2);
    *tail++ = p->lfo_retrigger;
    *tail++ = p->filter_idle_hold;
    *tail++ = p->filter_low;
    *tail++ = p->drive;
    *tail++ = p->filter_env_mode;
    memcpy(tail, p->filter_adsr, TG_ADSR_STAGES);
    tail += TG_ADSR_STAGES;
    *tail++ = p->accent_tone;
    *tail = p->accent_level;
}

int tg_patch_decode(tg_patch *patch, const uint8_t in[TG_PATCH_WIRE_BYTES])
{
    tg_patch p;
    memset(&p, 0, sizeof p);
    unpack_patch(&p, in);
    const uint8_t *tail = in + TG_PARAMETERS + 7;
    p.env_wide = *tail++;
    if (tail[0] > 4 || tail[1] > 4) return 0;
    p.osc_octave = (int8_t)((int)*tail++ - 2);
    p.key_octave = (int8_t)((int)*tail++ - 2);
    p.lfo_retrigger = *tail++;
    p.filter_idle_hold = *tail++;
    p.filter_low = *tail++;
    p.drive = *tail++;
    p.filter_env_mode = *tail++;
    memcpy(p.filter_adsr, tail, TG_ADSR_STAGES);
    tail += TG_ADSR_STAGES;
    p.accent_tone = *tail++;
    p.accent_level = *tail;
    if (!valid_patch(&p)) return 0;
    *patch = p;
    return 1;
}

/* The thirteen switch and octave fields in Telegram's patch wire take
 * 24 bits. Leave continuous controls and the 129-value ADSR fields intact. */
static const uint8_t patch_flag_widths[] = {2, 3, 2, 1, 2, 2, 2, 1, 3, 3, 1, 1, 1};
enum { PATCH_FLAGS = sizeof patch_flag_widths };

void tg_patch_pack_scene(uint8_t out[TG_PATCH_SCENE_BYTES], const tg_patch *patch)
{
    uint8_t wire[TG_PATCH_WIRE_BYTES];
    tg_patch_encode(wire, patch);
    memcpy(out, wire, TG_PARAMETERS);
    uint32_t flags = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < PATCH_FLAGS; i++) {
        flags |= (uint32_t)wire[TG_PARAMETERS + i] << shift;
        shift += patch_flag_widths[i];
    }
    for (unsigned i = 0; i < 3; i++) out[TG_PARAMETERS + i] = (uint8_t)(flags >> (i * 8));
    memcpy(out + TG_PARAMETERS + 3, wire + TG_PARAMETERS + PATCH_FLAGS,
           TG_PATCH_WIRE_BYTES - TG_PARAMETERS - PATCH_FLAGS);
}

int tg_patch_unpack_scene(tg_patch *patch, const uint8_t in[TG_PATCH_SCENE_BYTES])
{
    uint8_t wire[TG_PATCH_WIRE_BYTES];
    memcpy(wire, in, TG_PARAMETERS);
    uint32_t flags = in[TG_PARAMETERS] | (uint32_t)in[TG_PARAMETERS + 1] << 8 |
        (uint32_t)in[TG_PARAMETERS + 2] << 16;
    for (unsigned i = 0; i < PATCH_FLAGS; i++) {
        unsigned width = patch_flag_widths[i];
        wire[TG_PARAMETERS + i] = (uint8_t)(flags & ((1u << width) - 1u));
        flags >>= width;
    }
    memcpy(wire + TG_PARAMETERS + PATCH_FLAGS, in + TG_PARAMETERS + 3,
           TG_PATCH_WIRE_BYTES - TG_PARAMETERS - PATCH_FLAGS);
    return tg_patch_decode(patch, wire);
}

static void pack_library_patch(uint8_t out[TG_PATCH_STORE_BYTES], const tg_patch *patch)
{
    uint8_t scene[TG_PATCH_SCENE_BYTES];
    tg_patch_pack_scene(scene, patch);
    unsigned bits = 0, available = 0, at = 0;
    for (unsigned i = 0; i < TG_PARAMETERS; i++) {
        bits |= (unsigned)scene[i] << available;
        available += 7;
        if (available >= 8) {
            out[at++] = (uint8_t)bits;
            bits >>= 8;
            available -= 8;
        }
    }
    if (available) out[at++] = (uint8_t)bits;
    memcpy(out + at, scene + TG_PARAMETERS, sizeof scene - TG_PARAMETERS);
}

static int unpack_library_patch(tg_patch *patch, const uint8_t in[TG_PATCH_STORE_BYTES])
{
    uint8_t scene[TG_PATCH_SCENE_BYTES];
    unsigned bits = 0, available = 0, at = 0;
    for (unsigned i = 0; i < TG_PARAMETERS; i++) {
        if (available < 7) {
            bits |= (unsigned)in[at++] << available;
            available += 8;
        }
        scene[i] = (uint8_t)(bits & 127);
        bits >>= 7;
        available -= 7;
    }
    if (bits) return 0; /* Canonical zero padding. */
    memcpy(scene + TG_PARAMETERS, in + at, sizeof scene - TG_PARAMETERS);
    return tg_patch_unpack_scene(patch, scene);
}

static int encode(uint8_t *im,const tg_library *l,uint32_t generation, unsigned compact)
{
    if (l->profile >= TG_LAYOUT_PROFILES) return TG_STORE_INVALID;
    for (unsigned i = 0; i < TG_LAYOUT_PROFILES; i++)
        if (!valid_layout(&l->layouts[i], TG_CONTROL_COUNT)) return TG_STORE_INVALID;
    memset(im,255,TG_STORE_BANK);
    unsigned at=HEADER;
    put(im,&at,l->patches_present,8); put(im,&at,l->phrases_present,8);
    for (unsigned i=0;i<TG_USER_SLOTS;i++) {
        tg_saved_patch p;
        memset(&p,0,sizeof p);
        if (present(l->patches_present,i)) {
            p=l->patches[i];
            if (!valid_name(p.name) || !valid_patch(&p.patch)) return TG_STORE_INVALID;
        }
        put(im,&at,p.name,TG_NAME_BYTES);
        if (compact) {
            uint8_t wire[TG_PATCH_STORE_BYTES];
            pack_library_patch(wire, &p.patch);
            put(im, &at, wire, sizeof wire);
        } else {
            uint8_t wire[TG_PARAMETERS + 7];
            pack_patch(wire, &p.patch);
            put(im, &at, wire, sizeof wire);
        }
    }
    for (unsigned i=0;i<TG_USER_SLOTS;i++) {
        tg_saved_phrase p;
        memset(&p,0,sizeof p);
        if (present(l->phrases_present,i)) {
            p=l->phrases[i];
            if (!valid_name(p.name) || !valid_phrase(&p)) return TG_STORE_INVALID;
        }
        put(im,&at,p.name,TG_NAME_BYTES); put(im,&at,&p.length,1); put(im,&at,p.steps,TG_STEPS);
    }
    put(im, &at, &l->layouts[0], TG_LAYOUT_BYTES);
    if (!compact) {
        for (unsigned i=0;i<TG_USER_SLOTS;i++) {
            uint8_t offsets[2]={0,0};
            if (present(l->patches_present,i)) {
                offsets[0]=(uint8_t)l->patches[i].patch.osc_octave;
                offsets[1]=(uint8_t)l->patches[i].patch.key_octave;
            }
            put(im,&at,offsets,2);
        }
        for (unsigned i=0;i<TG_USER_SLOTS;i++) {
            uint8_t wide=present(l->patches_present,i) ? l->patches[i].patch.env_wide : 0;
            put(im,&at,&wide,1);
        }
        for (unsigned i=0;i<TG_USER_SLOTS;i++) {
            uint8_t low=present(l->patches_present,i) ? l->patches[i].patch.filter_low : 0;
            put(im,&at,&low,1);
        }
        for (unsigned i=0;i<TG_USER_SLOTS;i++) {
            uint8_t retrigger=present(l->patches_present,i) ? l->patches[i].patch.lfo_retrigger : 0;
            put(im,&at,&retrigger,1);
        }
        for (unsigned i=0;i<TG_USER_SLOTS;i++) {
            uint8_t hold=present(l->patches_present,i) ? l->patches[i].patch.filter_idle_hold : 0;
            put(im,&at,&hold,1);
        }
        for (unsigned i=0;i<TG_USER_SLOTS;i++) {
            uint8_t drive=present(l->patches_present,i) ? l->patches[i].patch.drive : 0;
            put(im,&at,&drive,1);
        }
    }
    for (unsigned i = 1; i < TG_LAYOUT_PROFILES; i++)
        put(im, &at, &l->layouts[i], TG_LAYOUT_BYTES);
    put(im, &at, &l->profile, 1);
    if (!compact) {
        for (unsigned i = 0; i < TG_USER_SLOTS; i++) {
            uint8_t filter_env[FILTER_ENV_BYTES] = {0};
            if (present(l->patches_present, i)) {
                filter_env[0] = l->patches[i].patch.filter_env_mode;
                memcpy(filter_env + 1, l->patches[i].patch.filter_adsr, TG_ADSR_STAGES);
            }
            put(im, &at, filter_env, sizeof filter_env);
        }
        for (unsigned i = 0; i < TG_USER_SLOTS; i++) {
            const tg_patch *p = &l->patches[i].patch;
            uint8_t accents[2] = {0};
            if (present(l->patches_present, i)) {
                accents[0] = p->accent_tone;
                accents[1] = p->accent_level;
            }
            put(im, &at, accents, sizeof accents);
        }
    }
    for (unsigned i = 0; i < TG_USER_SLOTS; i++) {
        uint8_t gate = present(l->phrases_present, i) ? l->phrases[i].gate : 0;
        put(im, &at, &gate, 1);
    }
    unsigned size = compact ? PACKED_PAYLOAD : PAYLOAD;
    memcpy(im,magic,8); wr32(im+8,compact ? 15 : 14); wr32(im+12,generation); wr32(im+16,size);
    wr32(im+20,crc(im,HEADER,size)); wr32(im+24,crc(im,0,24));
    return 0; /* Commit bytes stay erased until data readback completes. */
}
static int recognized_header(const uint8_t *im)
{
    uint32_t version=rd32(im+8), length=rd32(im+16);
    return !memcmp(im,magic,8) && rd32(im+24)==crc(im,0,24) &&
        ((version==15 && length==PACKED_PAYLOAD) || (version==14 && length==PAYLOAD) || ((version==12 || version==13) && length==V13_PAYLOAD) || (version==11 && length==V11_PAYLOAD) || (version==10 && length==V10_PAYLOAD) || (version==9 && length==V9_PAYLOAD) || (version==8 && length==V8_PAYLOAD) || (version==7 && length==V7_PAYLOAD) || (version==6 && length==V6_PAYLOAD) || (version==5 && length==V5_PAYLOAD) || (version==4 && length==V4_PAYLOAD) || (version==3 && length==V3_PAYLOAD) || ((version==1 || version==2) && length==OLD_PAYLOAD));
}
static int decode(const uint8_t *im,tg_library *l)
{
    if (!recognized_header(im) || rd32(im+28)!=COMMIT ||
        rd32(im+20)!=crc(im,HEADER,rd32(im+16))) return 0;
    for (unsigned sector=0;sector<TG_STORE_BANK;sector+=TG_STORE_SECTOR)
        for (unsigned i=USABLE;i<TG_STORE_SECTOR;i++) if(im[sector+i]!=255) return 0;
    unsigned at=HEADER;
    unsigned version = rd32(im + 8);
    uint8_t patches[8],phrases[8];
    get(im,&at,patches,8); get(im,&at,phrases,8);
    for (unsigned i=0;i<TG_USER_SLOTS;i++) {
        tg_saved_patch p;
        memset(&p,0,sizeof p);
        get(im,&at,p.name,TG_NAME_BYTES);
        if (version == 15) {
            uint8_t wire[TG_PATCH_STORE_BYTES];
            get(im, &at, wire, sizeof wire);
            if (!unpack_library_patch(&p.patch, wire)) return 0;
        } else {
            uint8_t wire[TG_PARAMETERS + 7];
            get(im, &at, wire, sizeof wire);
            unpack_patch(&p.patch, wire);
        }
        if (present(patches,i) && (!valid_name(p.name) || !valid_patch(&p.patch) ||
                                   (version < 13 && p.patch.pwm > TG_PWM_ENV))) return 0;
        if (rd32(im+8)==1) {
            p.patch.value[TG_FILTER_LFO]=(uint8_t)TG_FILTER_MOD_FROM_V1(p.patch.value[TG_FILTER_LFO]);
            p.patch.value[TG_MOD_FILTER]=(uint8_t)TG_FILTER_MOD_FROM_V1(p.patch.value[TG_MOD_FILTER]);
        }
        if (l) l->patches[i]=p;
    }
    for (unsigned i=0;i<TG_USER_SLOTS;i++) {
        tg_saved_phrase p;
        memset(&p,0,sizeof p);
        get(im,&at,p.name,TG_NAME_BYTES); get(im,&at,&p.length,1); get(im,&at,p.steps,TG_STEPS);
        if (present(phrases,i) && (!valid_name(p.name) || !valid_phrase(&p))) return 0;
        if (l) l->phrases[i]=p;
    }
    tg_layout layout;
    unsigned control_count = version >= 13 ? TG_CONTROL_COUNT :
                             version == 12 ? TG_CONTROL_PWM_SOURCE :
                             version == 11 ? TG_CONTROL_ACCENT_TONE : TG_CONTROL_FILTER_ATTACK;
    tg_layout_default(&layout);
    if (rd32(im+8)>=3) {
        get(im, &at, &layout, TG_LAYOUT_BYTES);
        if (!valid_layout(&layout, control_count)) return 0;
        if (rd32(im+8)==3) {
            for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(layout.knobs[b][i]>=TG_CONTROL_OSC_OCTAVE) return 0;
            for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(layout.faders[b][i]>=TG_CONTROL_OSC_OCTAVE) return 0;
        }
    }
    if (rd32(im+8)<5) {
        for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(layout.knobs[b][i]>=TG_CONTROL_ENV_WIDE) return 0;
        for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(layout.faders[b][i]>=TG_CONTROL_ENV_WIDE) return 0;
    }
    if (rd32(im+8)<6) {
        for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(layout.knobs[b][i]>=TG_CONTROL_FILTER_LOW) return 0;
        for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(layout.faders[b][i]>=TG_CONTROL_FILTER_LOW) return 0;
    }
    if (rd32(im+8)<7) {
        for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(layout.knobs[b][i]>=TG_CONTROL_LFO_RETRIGGER) return 0;
        for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(layout.faders[b][i]>=TG_CONTROL_LFO_RETRIGGER) return 0;
    }
    if (rd32(im+8)<8) {
        for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(layout.knobs[b][i]>=TG_CONTROL_FILTER_IDLE_HOLD) return 0;
        for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(layout.faders[b][i]>=TG_CONTROL_FILTER_IDLE_HOLD) return 0;
    }
    if (rd32(im+8)<9) {
        for(unsigned b=0;b<2;b++) for(unsigned i=0;i<8;i++) if(layout.knobs[b][i]>=TG_CONTROL_DRIVE) return 0;
        for(unsigned b=0;b<4;b++) for(unsigned i=0;i<4;i++) if(layout.faders[b][i]>=TG_CONTROL_DRIVE) return 0;
    }
    if (version < 15 && version >= 4) for(unsigned i=0;i<TG_USER_SLOTS;i++) {
        uint8_t wire[2];get(im,&at,wire,2);
        int osc=wire[0]<128 ? wire[0] : (int)wire[0]-256;
        int key=wire[1]<128 ? wire[1] : (int)wire[1]-256;
        if(osc<-2 || osc>2 || key<-2 || key>2) return 0;
        if(l) { l->patches[i].patch.osc_octave=(int8_t)osc;l->patches[i].patch.key_octave=(int8_t)key; }
    }
    if (version < 15 && version >= 5) for(unsigned i=0;i<TG_USER_SLOTS;i++) {
        uint8_t wide;get(im,&at,&wide,1);
        if(wide>1) return 0;
        if(l) l->patches[i].patch.env_wide=wide;
    }
    if (version < 15 && version >= 6) for(unsigned i=0;i<TG_USER_SLOTS;i++) {
        uint8_t low;get(im,&at,&low,1);
        if(low>1) return 0;
        if(l) l->patches[i].patch.filter_low=low;
    }
    if (version < 15 && version >= 7) for(unsigned i=0;i<TG_USER_SLOTS;i++) {
        uint8_t retrigger;get(im,&at,&retrigger,1);
        if(retrigger>1) return 0;
        if(l) l->patches[i].patch.lfo_retrigger=retrigger;
    }
    if (version < 15 && version >= 8) for(unsigned i=0;i<TG_USER_SLOTS;i++) {
        uint8_t hold;get(im,&at,&hold,1);
        if(hold>1) return 0;
        if(l) l->patches[i].patch.filter_idle_hold=hold;
    }
    if (version < 15 && version >= 9) for(unsigned i=0;i<TG_USER_SLOTS;i++) {
        uint8_t drive;get(im,&at,&drive,1);
        if(drive>127) return 0;
        if(l) l->patches[i].patch.drive=drive;
    }
    if (l) l->layouts[0] = layout;
    uint8_t profile = 0;
    for (unsigned i = 1; i < TG_LAYOUT_PROFILES; i++) {
        if (rd32(im + 8) >= 10) {
            get(im, &at, &layout, TG_LAYOUT_BYTES);
            if (!valid_layout(&layout, control_count)) return 0;
        } else tg_layout_default(&layout);
        if (l) l->layouts[i] = layout;
    }
    if (rd32(im + 8) >= 10) {
        get(im, &at, &profile, 1);
        if (profile >= TG_LAYOUT_PROFILES) return 0;
    }
    if (version < 15 && version >= 11) for (unsigned i = 0; i < TG_USER_SLOTS; i++) {
        uint8_t wire[FILTER_ENV_BYTES];
        get(im, &at, wire, sizeof wire);
        if (wire[0] > TG_FILTER_ENV_CLOSE) return 0;
        for (unsigned j = 1; j < sizeof wire; j++) if (wire[j] > 128) return 0;
        if (l) {
            l->patches[i].patch.filter_env_mode = wire[0];
            memcpy(l->patches[i].patch.filter_adsr, wire + 1, TG_ADSR_STAGES);
        }
    }
    if (version >= 12 && version < 15) for (unsigned i = 0; i < TG_USER_SLOTS; i++) {
        uint8_t accents[2];
        get(im, &at, accents, sizeof accents);
        if (accents[0] > 127 || accents[1] > 127) return 0;
        if (l) {
            l->patches[i].patch.accent_tone = accents[0];
            l->patches[i].patch.accent_level = accents[1];
        }
    }
    if (version >= 14) for (unsigned i = 0; i < TG_USER_SLOTS; i++) {
        uint8_t gate;
        get(im, &at, &gate, 1);
        if (!valid_phrase_gate(gate)) return 0;
        if (l) l->phrases[i].gate = gate;
    }
    if (l) {
        memcpy(l->patches_present, patches, 8);
        memcpy(l->phrases_present, phrases, 8);
        l->profile = profile;
    }
    return 1;
}
static const uint8_t tail_magic[4] = {'T','G','E',1};
typedef struct { unsigned offset, size; uint32_t sum; } journal_tail;

static int inspect_tail(const uint8_t *im, journal_tail *tail)
{
    unsigned version = rd32(im + 8);
    unsigned at = HEADER + (version == 15 ? PACKED_PAYLOAD : PAYLOAD);
    unsigned first = at;
    while (first < USABLE * 4 && im[physical(first)] == 255) first++;
    if (first == USABLE * 4) return version == 15 ? TG_STORE_INVALID : TG_STORE_EMPTY;
    if (version < 14) return TG_STORE_INVALID;
    unsigned capacity = version == 15 ? TG_STORE_TAIL_MAX : TG_STORE_V14_TAIL_MAX;
    uint8_t header[TAIL_HEADER];
    get(im, &at, header, sizeof header);
    unsigned size = rd32(header + 4);
    uint32_t sum = rd32(header + 8);
    if (memcmp(header, tail_magic, sizeof tail_magic) || !size || size > capacity ||
        sum != crc(im, at, size)) return TG_STORE_INVALID;
    *tail = (journal_tail){.offset = at, .size = size, .sum = sum};
    return TG_STORE_OK;
}

static int current(const tg_store_io *io, uint8_t *im, uint32_t *generation,
                   unsigned allow_core_only, unsigned *has_compact)
{
    unsigned core[2], complete[2], blank = 1;
    uint32_t gen[2], sum[2];
    journal_tail tails[2] = {{0}};
    if (has_compact) *has_compact = 0;
    for (unsigned i = 0; i < 2; i++) {
        if (io->read(io->context, i * TG_STORE_BANK, im, TG_STORE_BANK)) return TG_STORE_IO;
        core[i] = decode(im, 0);
        complete[i] = core[i] && inspect_tail(im, &tails[i]) >= 0;
        if (has_compact && core[i] && rd32(im + 8) == 15) *has_compact = 1;
        gen[i] = rd32(im + 12);
        sum[i] = rd32(im + 20);
        for (unsigned j = 0; blank && j < TG_STORE_BANK; j++) if (im[j] != 255) blank = 0;
    }
    /* Prefer a whole generation. Core-only salvage is read-only. */
    const unsigned *valid = complete;
    if (allow_core_only && !complete[0] && !complete[1]) valid = core;
    /* Do not implicitly format unknown or wholly corrupted contents. */
    if (!valid[0] && !valid[1]) { *generation=0; return blank?2:TG_STORE_INVALID; }
    unsigned bank=valid[0]?0:1;
    if (valid[0] && valid[1]) {
        uint32_t distance=gen[1]-gen[0];
        if (distance == UINT32_C(0x80000000) ||
            (!distance && (sum[0] != sum[1] || tails[0].size != tails[1].size || tails[0].sum != tails[1].sum)))
            return TG_STORE_CONFLICT;
        bank=distance && distance<UINT32_C(0x80000000)?1:0;
    }
    *generation=gen[bank]; return (int)bank;
}
int tg_store_load(const tg_store_io *io,tg_library *l,uint8_t *im)
{
    uint32_t generation;
    int bank=current(io,im,&generation,1,NULL);
    if (bank<0) return bank;
    if (bank==2) return TG_STORE_EMPTY;
    if (io->read(io->context,(unsigned)bank*TG_STORE_BANK,im,TG_STORE_BANK)) return TG_STORE_IO;
    if (!decode(im,0) || rd32(im + 12) != generation) return TG_STORE_INVALID;
    decode(im,l); return 0;
}
int tg_store_can_save(const tg_store_io *io,uint8_t *im)
{
    for (unsigned bank=0;bank<2;bank++) {
        if (io->read(io->context,bank*TG_STORE_BANK,im,TG_STORE_BANK)) return TG_STORE_IO;
        unsigned blank=1;
        for (unsigned i=0;i<TG_STORE_BANK;i++) if (im[i]!=255) blank=0;
        if (blank) continue;
        if (!recognized_header(im))
            return TG_STORE_INVALID;
        for (unsigned sector=0;sector<TG_STORE_BANK;sector+=TG_STORE_SECTOR)
            for (unsigned i=USABLE;i<TG_STORE_SECTOR;i++) if (im[sector+i]!=255) return TG_STORE_INVALID;
    }
    uint32_t generation;
    int bank=current(io,im,&generation,0,NULL);
    return bank<0?bank:TG_STORE_OK;
}
void tg_store_loaded_identity(const uint8_t *scratch, uint32_t *generation, uint32_t *library_crc)
{
    *generation = rd32(scratch + 12);
    *library_crc = rd32(scratch + 20);
}

int tg_store_loaded_tail(const uint8_t *im, uint8_t *tail, unsigned size, unsigned *actual)
{
    if (!tail || !size || size > TG_STORE_TAIL_MAX) return TG_STORE_INVALID;
    journal_tail info;
    int rc = inspect_tail(im, &info);
    if (rc) return rc;
    if (actual ? info.size > size : info.size != size) return TG_STORE_INVALID;
    get(im, &info.offset, tail, info.size);
    if (actual) *actual = info.size;
    return TG_STORE_OK;
}

int tg_store_load_tail(const tg_store_io *io, uint8_t *tail, unsigned size, uint8_t *im)
{
    if (!tail || !size || size > TG_STORE_TAIL_MAX) return TG_STORE_INVALID;
    uint32_t generation;
    int bank = current(io, im, &generation, 0, NULL);
    if (bank < 0) return bank;
    if (bank == 2) return TG_STORE_EMPTY;
    if (io->read(io->context, (unsigned)bank * TG_STORE_BANK, im, TG_STORE_BANK)) return TG_STORE_IO;
    if (!decode(im, 0) || rd32(im + 12) != generation) return TG_STORE_INVALID;

    return tg_store_loaded_tail(im, tail, size, NULL);
}

int tg_store_save_tail(const tg_store_io *io,const tg_library *l,const uint8_t *tail,
                       unsigned size,uint8_t *im)
{
    if (size > TG_STORE_TAIL_MAX || (size && !tail)) return TG_STORE_INVALID;
    uint32_t generation;
    unsigned has_compact;
    int bank=current(io,im,&generation,0,&has_compact);
    if (bank<0) return bank;
    uint32_t base=bank==0?TG_STORE_BANK:0;
    unsigned compact = size > TG_STORE_V14_TAIL_MAX;
    /* A corrupt four-lane tail still does not authorize a smaller journal. */
    if (has_compact && !compact) return TG_STORE_INVALID;
    int rc=encode(im,l,generation+1,compact);
    if (rc) return rc;
    if (size) {
        unsigned tail_at = HEADER + (compact ? PACKED_PAYLOAD : PAYLOAD);
        unsigned at = tail_at + TAIL_HEADER;
        put(im, &at, tail, size);
        uint8_t header[TAIL_HEADER];
        memcpy(header, tail_magic, sizeof tail_magic);
        wr32(header + 4, size);
        wr32(header + 8, crc(im, tail_at + TAIL_HEADER, size));
        at = tail_at;
        put(im, &at, header, sizeof header);
    }
    for (unsigned off=0;off<TG_STORE_BANK;off+=TG_STORE_SECTOR)
        if (io->erase(io->context,base+off)) return TG_STORE_IO;
    for (unsigned off=0;off<TG_STORE_BANK;off+=PAGE) {
        if (off%TG_STORE_SECTOR==USABLE) continue;
        if (io->program(io->context,base+off,im+off,PAGE)) return TG_STORE_IO;
    }
    uint8_t check[PAGE];
    for (unsigned off=0;off<TG_STORE_BANK;off+=PAGE) {
        if (io->read(io->context,base+off,check,PAGE) || memcmp(check,im+off,PAGE)) return TG_STORE_IO;
    }
    uint8_t marker[4]; wr32(marker,COMMIT);
    if (io->program(io->context,base+28,marker,4)) return TG_STORE_IO;
    if (io->read(io->context,base,im,TG_STORE_BANK)) return TG_STORE_IO;
    journal_tail verified;
    return decode(im,0) && inspect_tail(im, &verified) >= 0 ? TG_STORE_OK : TG_STORE_IO;
}

int tg_store_save(const tg_store_io *io,const tg_library *l,uint8_t *im)
{
    return tg_store_save_tail(io, l, NULL, 0, im);
}
