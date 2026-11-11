/* SPDX-License-Identifier: GPL-3.0-only */
/* Quiesce audio only after the current controls have reached the renderer. */
static void update_clock(void);
static int library_hardware_fault(void)
{
    return safe_mode || audio_fault || updating || usb.ota_req || usb.uboot_req;
}
/* A lost USB clock can resume the current bar. Save only after Stop. */
static unsigned library_transport_busy(const tg_controls *controls)
{
    return controls->running ||
           (controls->clock.source == TG_CLOCK_USB && controls->clock.state != TG_CLOCK_STOPPED);
}
static int flash_chip_ok(void)
{
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    uint32_t id = FL_FAR(fl_jedec_ram)();
    fm1_icfg_set(config);
    return FL_JEDEC_OK(id);
}
static int library_pause_audio(void)
{
    lcd_sync();
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    unsigned ready = !library_hardware_fault() && !fm1_in.keys && !smk_pad_own && smk_padq_r == smk_padq_w &&
                     !memcmp(&requested, &applied, sizeof requested) &&
                     !library_transport_busy(&requested) && !requested.live_note && !requested.arp_count && audio_quiet_blocks >= 4;
#ifdef ELITE_DRUMS808
    /* A press and release can both reach foreground before the next ISR. */
    ready = ready && launch_requested.drum_auditions[0].generation == launch.drums.audition_seen &&
            launch_requested.drum_auditions[1].generation == launch.drums_b.audition_seen;
#endif
    if (ready) audio_stop();
    fm1_icfg_set(config);
    return (int)ready;
}
static void audio_restart(void)
{
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    memset(dma, 0, sizeof dma);
    seen_half = 0;
    audio_quiet_blocks = 0;
    input_ticks = 0;
    last_half = fm1_ticks();
    /* Preserve voice and effects state while restarting DMA and cadence tracking. */
    if (!safe_mode && !audio_fault) fm1_audio_resume(dma, HALF_WORDS);
    fm1_icfg_set(config);
    update_clock();
}
