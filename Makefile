CC = clang
CPPFLAGS += -Ivendor/bb -Ivendor/bb/x0x -Ivendor/smk37 -DBASS303_CTRL=1
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wno-unused-function -O2 -ffp-contract=off
DSP = elite.c vendor/bb/bb_core.c vendor/bb/bb_dillo.c vendor/bb/bb_telegram.c \
      vendor/bb/bb_output.c vendor/bb/x0x/drum808.c vendor/bb/x0x/bass303.c
HEADERS = elite.h fixture.h conductor/dry_bus.h $(wildcard vendor/bb/*.h vendor/bb/*.inc vendor/bb/x0x/*.h vendor/smk37/*.h)

.PHONY: all check render model drum-cache
all: build/elite-render

build:
	mkdir -p build

build/elite-render: render.c fixture.c $(DSP) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) render.c fixture.c $(DSP) -o $@

build/test-elite: test_elite.c fixture.c $(DSP) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	  test_elite.c fixture.c $(DSP) -o $@

check: build/test-elite
	./build/test-elite

.PHONY: check-727
check-727: build/test-layer727 build/test-conductor-727 build/test-percussion-panel build/test-percussion-flash build/test-percussion-load
	./build/test-percussion-load
	python3 tools/test_provision_727.py
	./build/test-layer727
	./build/test-conductor-727
	./build/test-percussion-panel
	./build/test-percussion-flash

build/test-percussion-flash: board/test_percussion_flash.c board/percussion_flash.h board/library_region.h drums/layer727.h board/vendor/firmware/hal/fm1_flash_map.h board/vendor/firmware/hal/fm1_cc.h board/vendor/firmware/stub/stub_core.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iboard/vendor/firmware/hal -DBOARD_SMK37=1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@

build/test-percussion-load: board/test_percussion_load.c board/percussion_load.h board/percussion_flash.h board/library_region.h drums/layer727.h board/vendor/firmware/hal/fm1_flash_map.h board/vendor/firmware/stub/stub_core.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iboard/vendor/firmware/hal -DBOARD_SMK37=1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@

build/test-layer727: drums/test_layer727.c drums/layer727.c drums/layer727.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

render: build/elite-render
	mkdir -p build/auditions
	@set -e; for scene in mix drum-a drum-b dillo telegram dense; do \
	  ./build/elite-render $$scene build/auditions/$$scene.wav; \
	done

# Generic 32-bit object and stack accounting. This does not link firmware.
model: | build
	mkdir -p build/model
	@set -e; for source in $(DSP) tools/size_model.c; do \
	  object=$$(basename "$$source" .c); \
	  $(CC) --target=armv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard \
	    -ffreestanding -fstack-usage -ffunction-sections -fdata-sections \
	    -Itools/model-headers $(CPPFLAGS) $(CFLAGS) -c "$$source" -o "build/model/$$object.o"; \
	done

# Generate the exact data consumed by the board-only cached drum profile.
drum-cache: build/drum_cache.inc

build/drum_cache.inc: tools/drum_cache.c board/drum_pcm.h fixture.c $(DSP) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -Iboard tools/drum_cache.c fixture.c $(DSP) -o build/drum-cache
	./build/drum-cache > $@.tmp
	mv $@.tmp $@

build/test-telegram101: synth/test_telegram101.c synth/telegram101.c synth/presets.c synth/fx101.c synth/fx101.h synth/telegram101.h synth/filter101.inc | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@ -lm

build/test-poly101: synth/test_poly101.c synth/poly101.c synth/poly101.h synth/telegram101.c synth/telegram101.h synth/clock101.h synth/presets.c synth/filter101.inc | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

.PHONY: check-101
check-101: build/test-sideman101 build/test-fxtempo101 build/test-perform101 build/test-learn101 build/test-scope101 build/test-clockout101 build/test-miditrs101 build/test-poly101 build/test-telegram101 build/test-panel101 build/test-store101 build/test-library-board build/test-midi101 build/test-clock101 build/test-usb-reconnect
	./build/test-poly101
	./build/test-telegram101
	./build/test-panel101
	./build/test-store101
	./build/test-library-board
	./build/test-midi101
	./build/test-clock101
	./build/test-sideman101
	./build/test-fxtempo101
	./build/test-perform101
	./build/test-learn101
	./build/test-scope101
	./build/test-clockout101
	./build/test-miditrs101
	./build/test-usb-reconnect
	PYTHONDONTWRITEBYTECODE=1 python3 board/test_usb_clock.py
	PYTHONDONTWRITEBYTECODE=1 python3 board/test_usb_tx.py
	PYTHONDONTWRITEBYTECODE=1 python3 board/test_audio_resume.py
	PYTHONDONTWRITEBYTECODE=1 python3 board/test_bench_transport.py

build/test-store101: synth/test_store101.c synth/store101.c synth/store101.h synth/telegram101.c synth/presets.c synth/telegram101.h synth/filter101.inc | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

build/test-panel101: synth/test_panel101.c synth/catalog101.c synth/catalog101.h synth/telegram101.c synth/presets.c synth/store101.c synth/store101.h synth/telegram101.h synth/filter101.inc board/panel101.inc board/pad_feedback.inc | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

build/test-library-board: synth/test_library_board.c synth/test_panel101.c synth/catalog101.c synth/catalog101.h synth/telegram101.c synth/presets.c synth/store101.c synth/store101.h synth/telegram101.h synth/filter101.inc board/panel101.inc board/pad_feedback.inc board/library_storage.inc board/library_audio.inc board/ota_lifecycle.inc board/usb_diag.h board/library_region.h board/vendor/firmware/hal/fm1_flash_map.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_library_board.c synth/catalog101.c synth/telegram101.c synth/presets.c synth/store101.c -o $@

build/test-midi101: synth/test_midi101.c synth/midi101.c synth/midi101.h synth/clock101.c synth/clock101.h synth/test_panel101.c synth/catalog101.c synth/catalog101.h synth/telegram101.c synth/presets.c synth/store101.c synth/store101.h synth/telegram101.h synth/filter101.inc board/panel101.inc board/pad_feedback.inc board/midi_performance.inc board/midi_output.inc | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_midi101.c synth/midi101.c synth/clock101.c synth/catalog101.c synth/telegram101.c synth/presets.c synth/store101.c -o $@

build/test-sideman101: synth/test_sideman101.c synth/sideman101.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_sideman101.c -o $@

build/test-fxtempo101: synth/test_fxtempo101.c synth/fx101.c synth/fx101.h | build
	$(CC) $(CFLAGS) -I. -Iconductor -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_fxtempo101.c synth/fx101.c -o $@

build/test-perform101: synth/test_perform101.c synth/perform101.c synth/perform101.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_perform101.c synth/perform101.c -lm -o $@

build/test-learn101: synth/test_learn101.c synth/learn101.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_learn101.c -o $@

build/test-scope101: synth/test_scope101.c synth/scope101.c synth/scope101.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_scope101.c synth/scope101.c -lm -o $@

build/test-clockout101: synth/test_clockout101.c synth/clockout101.c synth/clockout101.h synth/telegram101.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_clockout101.c synth/clockout101.c -o $@

build/test-miditrs101: synth/test_miditrs101.c synth/miditrs101.c synth/miditrs101.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_miditrs101.c synth/miditrs101.c -o $@

build/test-clock101: synth/test_clock101.c synth/clock101.c synth/clock101.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

build/test-usb-reconnect: board/test_usb_reconnect.c board/usb_reconnect.inc board/usb_diag.inc board/usb_diag.h | build
	$(CC) $(CFLAGS) -I. -g -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@

build/test-telegram101 build/test-panel101 build/test-store101 build/test-library-board build/test-midi101: synth/clock101.h

.PHONY: check-dillo
check-dillo: build/test-phrase303 build/test-layer303 build/test-dillo-panel
	./build/test-phrase303
	./build/test-layer303
	./build/test-dillo-panel

build/test-phrase303: dillo/test_phrase303.c dillo/phrase303.c dillo/phrase303.h vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@


build/test-layer303: dillo/test_layer303.c dillo/layer303.c vendor/bb/bb_core.c dillo/layer303.h dillo/phrase303.c dillo/phrase303.h vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c synth/telegram101.c synth/telegram101.h synth/presets.c synth/filter101.inc $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

build/test-dillo-panel: synth/test_panel101.c synth/catalog101.c synth/telegram101.c synth/presets.c synth/store101.c dillo/layer303.c vendor/bb/bb_core.c dillo/phrase303.c vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c synth/telegram101.h synth/filter101.inc dillo/layer303.h dillo/phrase303.h board/panel101.inc board/pad_feedback.inc $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

.PHONY: check-one-808
check-one-808: build/test-one-808
	./build/test-one-808

.SECONDEXPANSION:
build/test-one-808: synth/test_one808.c $$(CONDUCTOR_BOARD_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_one808.c $(filter-out synth/test_library_board.c synth/test_panel101.c,$(filter %.c,$(CONDUCTOR_BOARD_DEPS))) -o $@

# Needs the locally generated phrase bank: node tools/import_synth_patterns.cjs STUDIO BB1 build/synth-presets INDEX
.PHONY: check-duets
check-duets: build/test-duets
	./build/test-duets

build/test-duets: synth/test_one808.c build/synth-presets/factory_synth_phrases.inc $$(CONDUCTOR_BOARD_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild/synth-presets -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -DELITE_SYNTH_PRESETS -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_one808.c $(filter-out synth/test_library_board.c synth/test_panel101.c,$(filter %.c,$(CONDUCTOR_BOARD_DEPS))) -o $@

# Every LIBRARY shelf at once: phrase bank, Band presets and the 727 layer.
.PHONY: check-library-shelves
check-library-shelves: build/test-library-shelves
	./build/test-library-shelves

build/test-library-shelves: synth/test_one808.c build/synth-presets/factory_synth_phrases.inc build/band-presets/factory_band_presets.inc board/layer_rhythms.inc drums/layer727.c drums/layer727.h $$(CONDUCTOR_BOARD_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild/synth-presets -Ibuild/band-presets -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -DELITE_SYNTH_PRESETS -DELITE_BAND_PRESETS -DELITE_PERCUSSION_727 -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_one808.c drums/layer727.c $(filter-out synth/test_library_board.c synth/test_panel101.c,$(filter %.c,$(CONDUCTOR_BOARD_DEPS))) -o $@

.PHONY: check-band-presets
check-band-presets: build/test-band-presets
	./build/test-band-presets

build/test-band-presets: synth/test_one808.c build/synth-presets/factory_synth_phrases.inc build/band-presets/factory_band_presets.inc $$(CONDUCTOR_BOARD_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild/synth-presets -Ibuild/band-presets -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -DELITE_SYNTH_PRESETS -DELITE_BAND_PRESETS -g -fsanitize=address,undefined -fno-sanitize-recover=all synth/test_one808.c $(filter-out synth/test_library_board.c synth/test_panel101.c,$(filter %.c,$(CONDUCTOR_BOARD_DEPS))) -o $@

.PHONY: check-808
check-808: build/test-conductor-mixer build/test-layer808 build/test-808-panel build/test-conductor build/test-conductor-board build/test-conductor-midi build/render-panel-conductor
	./build/test-layer808
	./build/test-conductor-mixer
	./build/test-808-panel
	./build/test-conductor
	./build/test-conductor-board
	./build/test-conductor-midi
	./build/render-panel-conductor --dense > /dev/null
	./build/render-panel-conductor --dense-usb > /dev/null
	./build/render-panel-conductor --dense-kits > /dev/null
	./build/render-panel-conductor --dense --single-808 > /dev/null
	./build/render-panel-conductor --dense-usb --single-808 > /dev/null

DRUMS_DSP = drums/layer808.c vendor/bb/x0x/drum808.c
DRUMS_HEADERS = drums/layer808.h board/drum_presets.inc
build/test-layer808: drums/test_layer808.c $(DRUMS_DSP) $(DRUMS_HEADERS) dillo/layer303.c vendor/bb/bb_core.c dillo/layer303.h dillo/phrase303.c dillo/phrase303.h vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c synth/telegram101.c synth/telegram101.h synth/presets.c synth/filter101.inc $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

DRUM_PANEL_TEST = synth/test_panel101.c synth/catalog101.c synth/telegram101.c synth/presets.c synth/store101.c dillo/layer303.c vendor/bb/bb_core.c dillo/phrase303.c vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c $(DRUMS_DSP) $(DRUMS_HEADERS) conductor/launch.h conductor/mixer.h synth/fx101.h synth/telegram101.h synth/filter101.inc dillo/layer303.h dillo/phrase303.h board/conductor_storage.inc board/panel101.inc board/pad_feedback.inc board/dillo_pattern_sounds.inc $(HEADERS)

build/test-808-panel: $(DRUM_PANEL_TEST) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

build/test-percussion-panel: $(DRUM_PANEL_TEST) drums/layer727.c drums/layer727.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -DELITE_PERCUSSION_727 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

CONDUCTOR_DSP_TEST = conductor/test_launch.c conductor/launch.c conductor/launch.h conductor/mixer.h synth/fx101.h $(DRUMS_DSP) $(DRUMS_HEADERS) dillo/layer303.c vendor/bb/bb_core.c dillo/layer303.h dillo/phrase303.c dillo/phrase303.h vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c synth/clock101.c synth/clock101.h synth/telegram101.c synth/presets.c synth/telegram101.h synth/filter101.inc $(HEADERS)

build/test-conductor: $(CONDUCTOR_DSP_TEST) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

build/test-conductor-727: $(CONDUCTOR_DSP_TEST) drums/layer727.c drums/layer727.h board/percussion_bench.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_PERCUSSION_727 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@

CONDUCTOR_BOARD_DEPS = synth/test_library_board.c board/library_storage.inc board/library_audio.inc board/ota_lifecycle.inc board/usb_diag.h board/library_region.h board/vendor/firmware/hal/fm1_flash_map.h synth/test_panel101.c synth/catalog101.c synth/telegram101.c synth/presets.c synth/store101.c dillo/layer303.c vendor/bb/bb_core.c dillo/phrase303.c vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c $(DRUMS_DSP) $(DRUMS_HEADERS) conductor/launch.c conductor/launch.h conductor/mixer.h synth/fx101.h synth/telegram101.h synth/filter101.inc dillo/layer303.h dillo/phrase303.h board/conductor_storage.inc board/panel101.inc board/pad_feedback.inc board/dillo_pattern_sounds.inc $(HEADERS)

build/test-conductor-board: $(CONDUCTOR_BOARD_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter-out synth/test_panel101.c,$(filter %.c,$^)) -o $@

build/render-panel-conductor: tools/render_panel_conductor.c $(CONDUCTOR_BOARD_DEPS) conductor/mixer.c synth/fx101.c synth/clock101.c synth/clock101.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -DCONDUCTOR_DRY_STRIDE=1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter-out synth/test_panel101.c synth/test_library_board.c,$(filter %.c,$^)) -o $@

ELITE_VIEW_DEPS = board/instrument_view.inc board/view_chrome.inc board/picnic_aa.inc board/view_tel.inc board/sh101_charts.inc board/view_dillo.inc board/view_band.inc board/view_808.inc board/view_footer.inc board/view_focus.inc board/picnic_title.inc

build/font.inc: board/build.py board/vendor/font/ter-u16n.bdf | build
	PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=board python3 -c 'from pathlib import Path; from build import font_header; font_header(Path("board/vendor/font/ter-u16n.bdf"), Path("$@"))'

build/render-library101: tools/render_library101.c $(CONDUCTOR_BOARD_DEPS) $(ELITE_VIEW_DEPS) build/font.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild -DELITE_DILLO -DELITE_DRUMS808 $(filter-out synth/test_panel101.c synth/test_library_board.c,$(filter %.c,$^)) -o $@

build/render-duet: tools/render_duet.c $(CONDUCTOR_BOARD_DEPS) conductor/mixer.c synth/fx101.c synth/clock101.c synth/clock101.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -DCONDUCTOR_DRY_STRIDE=1 $(filter-out synth/test_panel101.c synth/test_library_board.c,$(filter %.c,$^)) -o $@

build/render-latin: tools/render_latin.c $(CONDUCTOR_BOARD_DEPS) conductor/mixer.c synth/fx101.c synth/clock101.c synth/clock101.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -DCONDUCTOR_DRY_STRIDE=1 $(filter-out synth/test_panel101.c synth/test_library_board.c,$(filter %.c,$^)) -o $@

build/render-one-808: tools/render_library101.c $(CONDUCTOR_BOARD_DEPS) $(ELITE_VIEW_DEPS) build/font.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 $(filter-out synth/test_panel101.c synth/test_library_board.c,$(filter %.c,$^)) -o $@

build/render-duets: tools/render_library101.c build/synth-presets/factory_synth_phrases.inc $(CONDUCTOR_BOARD_DEPS) $(ELITE_VIEW_DEPS) build/font.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild -Ibuild/synth-presets -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 -DELITE_SYNTH_PRESETS $(filter-out synth/test_panel101.c synth/test_library_board.c,$(filter %.c,$^)) -o $@

build/test-conductor-midi: synth/test_midi101.c synth/test_panel101.c synth/midi101.c synth/midi101.h synth/clock101.c synth/clock101.h synth/catalog101.c synth/catalog101.h synth/telegram101.c synth/telegram101.h synth/presets.c synth/store101.c synth/store101.h synth/filter101.inc conductor/launch.c conductor/launch.h conductor/mixer.h dillo/layer303.c dillo/layer303.h dillo/phrase303.c dillo/phrase303.h vendor/bb/bb_core.c vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c $(DRUMS_DSP) $(DRUMS_HEADERS) board/conductor_storage.inc board/panel101.inc board/pad_feedback.inc board/midi_performance.inc board/midi_output.inc $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -DELITE_DILLO -DELITE_DRUMS808 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter-out synth/test_panel101.c,$(filter %.c,$^)) -o $@

build/test-conductor-mixer: conductor/test_mixer.c conductor/mixer.c conductor/mixer.h conductor/dry_bus.h synth/fx101.c synth/fx101.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all $(filter %.c,$^) -o $@
