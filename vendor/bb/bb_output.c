#include "bb_output.h"
#include <string.h>
#include <limits.h>
static float magnitude(float x){return x<0?-x:x;}
static float clamp_output(float x){return x>0.98f?0.98f:x< -0.98f?-0.98f:x;}
/* All deque indices passed here are 0..64; avoid target integer division. */
static unsigned wrap_window(unsigned index){return index>=BB_OUTPUT_WINDOW?index-BB_OUTPUT_WINDOW:index;}
static void increment(uint32_t *p){if(*p!=UINT32_MAX)(*p)++;}
int bb_output_init(bb_output *p,unsigned rate,float boost) {
    if(!p || rate<8000 || rate>192000 || boost!=boost || boost<1 || boost>2)return 0;
    memset(p,0,sizeof *p);p->boost=boost;p->gain=p->required=1;
    /* Full attenuation -> unity takes 100ms, independent of stream rate. */
    p->release_step=10.0f/(float)rate;return 1;
}
int bb_output_process(bb_output *p,float *left,float *right,unsigned frames) {
    unsigned i;
    if(!p || !left || !right)return 0;
    for(i=0;i<frames;i++) {
        float l=left[i],r=right[i],peak,delayed_l,delayed_r,window;
        unsigned tail;
        /* Ordered bounds reject infinities; explicit unordered checks reject
         * NaN. Avoid the target SDK's unavailable __fpclassifyf helper. */
        if(l!=l || r!=r || magnitude(l)>64 || magnitude(r)>64) {
            l=r=0;increment(&p->faults);
        }
        peak=magnitude(l)>magnitude(r)?magnitude(l):magnitude(r);
        delayed_l=p->left[p->position];delayed_r=p->right[p->position];
        p->left[p->position]=l;p->right[p->position]=r;
        p->position=(p->position+1u)&(BB_OUTPUT_DELAY-1u);
        /* Bounded unsigned ages also work across the uint32 frame wrap. */
        while(p->count && (uint32_t)(p->frame-p->ages[p->head])>=BB_OUTPUT_WINDOW) {
            p->head=wrap_window(p->head+1u);p->count--;
        }
        while(p->count) {
            tail=wrap_window(p->head+p->count-1u);
            if(p->peaks[tail]>peak)break;
            p->count--;
        }
        tail=wrap_window(p->head+p->count);
        p->peaks[tail]=peak;p->ages[tail]=p->frame;p->count++;p->frame++;
        window=p->peaks[p->head];
        if(window!=p->cached_peak) {
            float boosted=window*p->boost; /* bounded by 128, no overflow */
            p->required=boosted>BB_OUTPUT_CEILING?BB_OUTPUT_CEILING/boosted:1;
            p->cached_peak=window;
        }
        p->gain=p->required<p->gain+p->release_step?p->required:p->gain+p->release_step;
        if(p->gain<1)increment(&p->limited);
        l=(delayed_l*p->boost)*p->gain;r=(delayed_r*p->boost)*p->gain;
        /* Roundoff/safety backstop, not the normal level-control mechanism.
         * This is below the retained adapter's .98 conversion guard. */
        left[i]=clamp_output(l);right[i]=clamp_output(r);
    }
    return 1;
}
