.PHONY: firmware verify-release-tree

MODEL ?= elite
JIELI_TOOLCHAIN ?=
DOCKER_IMAGE ?= fm1-timing-runtime:bookworm-python

ifeq ($(shell uname -s),Darwin)
BUILD = docker run --rm --platform linux/amd64 \
	-v "$(CURDIR):/work" -v "$(JIELI_TOOLCHAIN):/opt/jieli:ro" \
	-w /work $(DOCKER_IMAGE) python3 board/build.py --toolchain /opt/jieli
else
BUILD = python3 board/build.py --toolchain "$(JIELI_TOOLCHAIN)"
endif

firmware:
	@test -n "$(JIELI_TOOLCHAIN)" || { printf '%s\n' 'Set JIELI_TOOLCHAIN to the unpacked PI32 toolchain.' >&2; exit 2; }
	@test "$(MODEL)" = elite -o "$(MODEL)" = pro -o "$(MODEL)" = mke || { printf '%s\n' 'MODEL must be elite, pro or mke.' >&2; exit 2; }
	mkdir -p build
	PYTHONDONTWRITEBYTECODE=1 $(BUILD) \
	  --out "build/public-800-$(MODEL)" \
	  --build-number 800 \
	  --model "$(MODEL)" \
	  --synth-presets release-data/synth \
	  --band-presets release-data/band \
	  --dillo --drums --single-808 \
	  --render-profile --drum-render-in-ram --dillo-render-in-ram

verify-release-tree:
	./scripts/verify-release-tree.sh
