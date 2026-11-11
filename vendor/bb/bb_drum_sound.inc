/* SPDX-License-Identifier: GPL-3.0-only
 * Portable schema implementation, compiled as part of bb_core.c.
 * Defaults/order derive from pinned X0X drum808.c (8W8/sc808 sources), whose
 * original source/licences remain immutable. Named variations are new Big
 * Beat parameter recipes, NOT other historical machine emulations. */
const char *const bb_drum_kit_names[BB_DRUM_KITS]={
    "808 Reference","808 Short","808 Long","808 Drive",
    "808 Claves","808 Congas","808 Low","808 Tight Hats"};
static const uint8_t bb_drum_indices[BB_DRUM_TRACKS][2]={
    {0,0},{1,1},{2,5},{3,6},{4,7},{8,9},{11,10},{12,12},{15,15},{14,14},{13,13}};
unsigned bb_drum_sound_index(const bb_drum_sound *s,unsigned track) {
    return s && track<BB_DRUM_TRACKS?bb_drum_sound_variant(track,s->mode[track]):BB_DRUM_SOUNDS;
}
unsigned bb_drum_sound_variant(unsigned track,unsigned mode) {
    return track<BB_DRUM_TRACKS && mode<=1?bb_drum_indices[track][mode]:BB_DRUM_SOUNDS;
}
static int bb_drum_slot(unsigned track,unsigned parameter) {
    static const int8_t pots[BB_DRUM_PARAMETER_COUNT]={0,1,2,3,4,-1,-1,-1,-1,5,6};
    if(track>=BB_DRUM_TRACKS || parameter>=BB_DRUM_PARAMETER_COUNT)return -1;
    if(parameter==BB_DRUM_TONE)return track==0?7:track==1?8:-1;
    if(parameter==BB_DRUM_ATTACK)return track==0?8:track==6?7:-1;
    if(parameter==BB_DRUM_SNAPPY)return track==1?7:-1;
    return pots[parameter];
}
int bb_drum_parameter_max(const bb_drum_sound *s,unsigned track,unsigned parameter) {
    if(!s || track>=BB_DRUM_TRACKS || parameter>=BB_DRUM_PARAMETER_COUNT)return -1;
    if(parameter==BB_DRUM_MODE)return track>=2 && track<=6?1:-1;
    if(bb_drum_slot(track,parameter)<0)return -1;
    return parameter==BB_DRUM_DIST?6:127;
}
const char *bb_drum_parameter_name(unsigned parameter) {
    static const char *const names[BB_DRUM_PARAMETER_COUNT]={
        "LEVEL","TUNE","DECAY","DRIVE","TYPE","TONE","ATTACK","SNAPPY","SOUND","SPRING","ECHO"};
    return parameter<BB_DRUM_PARAMETER_COUNT?names[parameter]:"--";
}
const char *bb_drum_voice_name(const bb_drum_sound *s,unsigned track) {
    static const char *const names[BB_DRUM_SOUNDS]={
        "BASS DRUM","SNARE","LOW TOM","MID TOM","HIGH TOM",
        "LOW CONGA","MID CONGA","HIGH CONGA","RIMSHOT","CLAVES",
        "MARACAS","HAND CLAP","COWBELL","CLOSED HAT","OPEN HAT","CYMBAL"};
    unsigned sound=bb_drum_sound_index(s,track);
    return sound<BB_DRUM_SOUNDS?names[sound]:"--";
}
unsigned bb_drum_parameter_get(const bb_drum_sound *s,unsigned track,unsigned parameter) {
    if(bb_drum_parameter_max(s,track,parameter)<0)return 0;
    if(parameter==BB_DRUM_MODE)return s->mode[track];
    /* The clap track's attack belongs to its independently retained maraca,
     * even while CLAP is selected, exactly as the upstream fixed mapping. */
    return s->pot[parameter==BB_DRUM_ATTACK && track==6?10:bb_drum_sound_index(s,track)][bb_drum_slot(track,parameter)];
}
int bb_drum_parameter_set(bb_drum_sound *s,unsigned track,unsigned parameter,unsigned value) {
    int max=bb_drum_parameter_max(s,track,parameter);
    uint8_t *dest;
    if(max<0 || value>(unsigned)max)return 0;
    dest=parameter==BB_DRUM_MODE?&s->mode[track]:&s->pot[parameter==BB_DRUM_ATTACK && track==6?10:bb_drum_sound_index(s,track)][bb_drum_slot(track,parameter)];
    if(*dest!=(uint8_t)value){*dest=(uint8_t)value;s->edited=1;}
    return 1;
}
int bb_drum_sound_recall(bb_drum_sound *s,unsigned kit) {
    static const uint8_t decay[BB_DRUM_SOUNDS]={87,108,68,56,53,58,36,34,40,64,51,64,62,47,74,70};
    unsigned i;
    if(!s || kit>=BB_DRUM_KITS)return 0;
    memset(s,0,sizeof *s);
    for(i=0;i<BB_DRUM_SOUNDS;i++) {
        s->pot[i][0]=64;s->pot[i][1]=64;s->pot[i][2]=decay[i];
    }
    s->pot[0][7]=42;s->pot[0][8]=24;s->pot[1][7]=89;s->pot[1][8]=64;s->pot[10][7]=32;
    s->kit_level=100;s->kit_accent=127;
    /* Sound-only recipes: never own steps, transport, faders or effect state.
     * Keep reference kit exact; variations deliberately start with dry sends. */
    switch(kit) {
    case 1:
        s->pot[0][2]=63;s->pot[1][2]=73;s->pot[13][2]=29;s->pot[14][2]=52;s->pot[15][2]=49;
        s->pot[1][7]=100;s->pot[0][8]=37;break;
    case 2:
        s->pot[0][2]=104;s->pot[0][1]=56;s->pot[1][2]=116;s->pot[14][2]=93;s->pot[15][2]=98;break;
    case 3:
        for(i=0;i<BB_DRUM_SOUNDS;i++)s->pot[i][3]=24;
        s->pot[0][3]=38;s->pot[1][3]=31;s->pot[1][7]=104;s->pot[0][7]=60;break;
    case 4:
        s->mode[5]=1;s->mode[6]=1;s->pot[0][2]=59;s->pot[1][2]=67;
        s->pot[0][7]=24;s->pot[1][7]=56;s->pot[9][0]=45;s->pot[10][0]=52;break;
    case 5:
        s->mode[2]=s->mode[3]=s->mode[4]=s->mode[5]=s->mode[6]=1;
        s->pot[0][2]=72;s->pot[1][0]=48;s->pot[13][0]=50;break;
    case 6:
        s->pot[0][1]=48;s->pot[0][2]=111;s->pot[0][7]=21;s->pot[1][1]=57;
        s->pot[1][7]=72;s->pot[14][2]=86;s->pot[15][0]=46;break;
    case 7:
        s->pot[0][2]=68;s->pot[1][2]=78;s->pot[13][2]=20;s->pot[14][2]=42;
        s->pot[15][2]=45;s->pot[13][1]=78;s->pot[14][1]=74;s->pot[12][1]=81;break;
    default:break;
    }
    return 1;
}
