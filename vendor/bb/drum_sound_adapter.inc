/* Map portable parameters onto the pinned engine's descriptor ordinals.
 * No resets or triggers here. Apply only changed pots at block boundaries;
 * temporarily selecting alternate sound banks never modifies lane.snd or
 * its ringing DSP state. Both banks are restored before scheduling hits. */
static void audio_drum_sound(drum808_t *d,const bb_drum_sound *s) {
    static const int8_t map[BB_DRUM_TRACKS][BB_DRUM_PARAMETER_COUNT]={
        {0,3,2,5,6,1,4,-1,-1,7,8},
        {0,3,4,5,6,1,-1,2,-1,7,8},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,5,6,-1,3,-1,4,7,8},
        {0,1,2,3,4,-1,-1,-1,-1,5,6},
        {0,2,1,3,4,-1,-1,-1,-1,5,6},
        {0,2,1,3,4,-1,-1,-1,-1,5,6},
        {0,1,2,3,4,-1,-1,-1,-1,5,6}};
    unsigned track,mode,p;
    for(track=0;track<BB_DRUM_TRACKS;track++) {
        unsigned banks=map[track][BB_DRUM_MODE]>=0?2u:1u;
        for(mode=0;mode<banks;mode++) {
            unsigned sound=bb_drum_sound_variant(track,mode);
            if(banks==2 && d->sw[track]!=mode)drum808_set(d,(int)track,map[track][BB_DRUM_MODE],(int)mode);
            for(p=0;p<BB_DRUM_PARAMETER_COUNT;p++) {
                int ordinal=map[track][p];unsigned value;
                if(ordinal<0 || p==BB_DRUM_MODE)continue;
                /* Canonical pot order is shared with the pinned DSP. The
                 * helper getter resolves snare/BD tone and fixed MA attack. */
                switch(p) {
                case BB_DRUM_LEVEL:value=s->pot[sound][D8P_LEVEL];break;
                case BB_DRUM_TUNE:value=s->pot[sound][D8P_TUNE];break;
                case BB_DRUM_DECAY:value=s->pot[sound][D8P_DECAY];break;
                case BB_DRUM_DRIVE:value=s->pot[sound][D8P_DRIVE];break;
                case BB_DRUM_DIST:value=s->pot[sound][D8P_DIST];break;
                case BB_DRUM_TONE:value=s->pot[sound][track==0?D8P_X1:D8P_X2];break;
                case BB_DRUM_ATTACK:value=track==6?s->pot[D8S_MA][D8P_X1]:s->pot[sound][D8P_X2];break;
                case BB_DRUM_SNAPPY:value=s->pot[sound][D8P_X1];break;
                case BB_DRUM_SPRING:value=s->pot[sound][D8P_REV];break;
                default:value=s->pot[sound][D8P_DLY];break;
                }
                if(drum808_get(d,(int)track,ordinal)!=(int)value)drum808_set(d,(int)track,ordinal,(int)value);
            }
        }
        if(banks==2 && d->sw[track]!=s->mode[track])drum808_set(d,(int)track,map[track][BB_DRUM_MODE],s->mode[track]);
    }
    if(d->kpot[0]!=s->kit_level)drum808_set(d,D8_KIT,0,s->kit_level);
    if(d->kpot[1]!=s->kit_accent)drum808_set(d,D8_KIT,1,s->kit_accent);
}
