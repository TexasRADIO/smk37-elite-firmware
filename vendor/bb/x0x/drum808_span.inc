/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat mono span specialization of the licensed X0X/8W8 renderer.
 * Included AFTER the generated drum808.c implementation, never in the pinned
 * reference. Voice equations are unchanged; this preserves rendering n calls
 * of drum808_render(...,1), not upstream's chunk-dependent n-frame behavior.
 * Triggers/parameter changes occur between spans. Buffers must not alias the
 * engine or each other. Adds to dry/reverb/delay; no static writable state.
 */
void bb_drum808_render_span(drum808_t *d, float *dry, float *rev, float *dly, int n)
{
    struct mix { float gain, k, trim, ra, da; int bypass, type; } mix[L_NUM];
    unsigned active = 0;
    int l, i;
    if(n<=0)return;
    /* These values change only at trigger/control boundaries, not in tick().
     * Preserve the reference's multiplication order (no reassociation/FMA). */
    for(l=0;l<L_NUM;l++)if(lane_on(d,l)) {
        const d8_lane_t *ln=&d->lane[l];
        int s=ln->snd;
        active|=1u<<l;
        mix[l].trim=k_trim[s];
        mix[l].gain=d->potv[s][D8P_LEVEL]*ln->hit*d->vol;
        mix[l].k=mix[l].trim*mix[l].gain*ln->cg;
        mix[l].ra=d->potv[s][D8P_REV];mix[l].da=d->potv[s][D8P_DLY];
        mix[l].bypass=d->shp[s].drive<1.0e-3f;
        mix[l].type=d->pot[s][D8P_DIST];
    }
    for(i=0;i<n && active;i++) {
        float bus=0, pair=0;
        if(active & ((1u<<L_CB)|(1u<<L_CH)|(1u<<L_OH)|(1u<<L_CY))) {
            d8_bank_t *b=&d->bank;
            bus=bank_tick(b);
            if(active&(1u<<L_CB))
                pair=((b->ph[4]<BANK_DUTY?2.5f:-2.5f)+(b->ph[5]<BANK_DUTY?2.5f:-2.5f))*0.5f;
        }
        for(l=0;l<L_NUM;l++)if(active&(1u<<l)) {
            d8_lane_t *ln=&d->lane[l];
            const struct mix *m=&mix[l];
            float v;
            switch(l) {
            case L_BD:v=bd_tick(&d->bd);break;
            case L_SD:v=sd_tick(&d->sd);break;
            case L_T0:case L_T1:case L_T2:v=tom_tick(&d->tom[l-L_T0]);break;
            case L_RS:v=rs_tick(&d->rs);break;
            case L_CL:v=cl_tick(&d->cl);break;
            case L_MA:v=ma_tick(&d->ma);break;
            case L_CP:v=cp_tick(&d->cp);break;
            case L_CB:v=cb_tick(&d->cb,pair);break;
            case L_CH:v=hat_tick(&d->ch,bus);break;
            case L_OH:v=hat_tick(&d->oh,bus);break;
            default:v=cy_tick(&d->cy,bus);break;
            }
            if(ln->cstep==0.0f && m->bypass) {
                dry[i]+=v*m->k;
                if(m->ra>0.0f)rev[i]+=v*m->k*m->ra;
                if(m->da>0.0f)dly[i]+=v*m->k*m->da;
            } else {
                if(ln->cstep<0.0f) {
                    ln->cg+=ln->cstep;
                    if(ln->cg<=0.0f){ln->cg=0.0f;ln->cstep=0.0f;}
                }
                if(ln->cg>0.0f) {
                    float x=v*m->trim, sv;
                    if(!m->bypass)x=shape(x,&d->shp[ln->snd],m->type,ln->crush);
                    sv=x*m->gain*ln->cg;
                    dry[i]+=sv;rev[i]+=sv*m->ra;dly[i]+=sv*m->da;
                }
            }
#if D8_TAIL_DB
            lane_tail(d,l,&v,1);
#endif
            if(!lane_on(d,l))active&=~(1u<<l);
        }
    }
}
