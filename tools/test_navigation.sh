#!/bin/sh
set -eu
# From the firmware root, pass the build directory containing the generated font.
build_dir=$1
shift
extra_flags=
extra_sources=
if python3 -c 'import json, sys; sys.exit(json.load(open(sys.argv[1]))["percussion_plan"] is None)' "$build_dir/build.json"; then
    extra_flags=-DELITE_PERCUSSION_727
    extra_sources="drums/layer727.c conductor/launch.c synth/clock101.c"
fi
cc -std=c11 -Os -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$build_dir" -Isynth -Ivendor/bb -Ivendor/bb/x0x -DBASS303_CTRL=1 \
    -Ibuild/synth-presets -Ibuild/band-presets -DELITE_SYNTH_PRESETS -DELITE_BAND_PRESETS \
    -DELITE_DILLO -DELITE_DRUMS808 -DELITE_SINGLE_808 $extra_flags \
    tools/test_navigation.c synth/scope101.c synth/catalog101.c synth/telegram101.c synth/presets.c \
    synth/store101.c dillo/layer303.c vendor/bb/bb_core.c dillo/phrase303.c \
    vendor/bb/bb_dillo.c vendor/bb/x0x/bass303.c drums/layer808.c \
    vendor/bb/x0x/drum808.c $extra_sources -o "$build_dir/test-navigation"
"$build_dir/test-navigation" "$@"
