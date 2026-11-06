# RoboSeq: the everyday commands. The firmware itself is built by ./build.sh (docs/DINGHY_README.md: the toolchain).
#   make test    the model against the HTML's traces, then the app on the host (that in Docker on macOS: Dinghy's
#                host mode needs ELF sections); seconds, no FM-1
#   make traces  re-make tests/golden/traces from reference/roboseq.html (node)
#   make reference  re-render tests/golden/html with Tone.Offline (node, a Chromium)
#   make parity  the C voices, mix and song against those renders
#   make song    the default song played on the host, PLAY at 0.5 s: build/host/song.wav
#   make play    keys played on the host (tests/roboseq/play.txt): build/host/play.wav and its screens
#   make scenes  the board's rarer moments (a clash, love, steering) drawn straight: build/scenes/*.png
#   make probe   the HTML's mix pieces (limiter, reverb, low-pass) measured in Chromium: build/probe (node)
#   make fw      the FM-1 package build/roboseq.fwsc (JieLi toolchain in ~/.jieli, SDK in ~/fw-AC79_AIoT_SDK)
#   make check   everything CI runs: the package, then Dinghy's platform tests on it
#   make emu     the package on fm1-emu, headless and FM1_STRICT: build/emu/panel.png (EMU=path/to/fm1-live)
#   make image   the Docker image make test uses on macOS
PY ?= $(shell for p in python3 /usr/bin/python3; do $$p -c 'import PIL' 2>/dev/null && { echo $$p; break; }; done)
ifeq ($(shell uname -s),Linux)
RUN =
RUN_SDK =
IMG =
else
DOCKER = docker run --rm --platform linux/amd64 -v "$(CURDIR)":/w -w /w
RUN = $(DOCKER) roboseq-dev
RUN_SDK = $(DOCKER) -v "$(HOME)/fw-AC79_AIoT_SDK":/sdk:ro -e AC79_SDK=/sdk roboseq-dev
IMG = image
endif

EMU ?= ../fm1-emu/target/release/fm1-live
EMU_MS ?= 6000
EMU_KEYS ?= 1000:PLAY

.PHONY: test host traces reference parity fw check emu image clean
test: $(IMG)
	sh tests/run_roboseq.sh
	$(RUN) sh tests/run_host.sh
host: test
traces:
	node tools/trace/gen.mjs
reference:       ## re-render tests/golden/html from the HTML's own buildAudio() (node, a Chromium: Brave here)
	cd tools/tone-ref && npm ci --no-fund --no-audit && node render.mjs
parity:          ## the C voices, mix and song against those renders (numpy)
	sh tests/run_roboseq.sh >/dev/null
	python3 tools/tone-ref/compare.py --check
song: $(IMG)
	$(RUN) sh -c 'cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/roboseq firmware/src/roboseq.c && build/host/roboseq build/host/song tests/roboseq/song.txt'
play: $(IMG)
	$(RUN) sh -c 'cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/roboseq firmware/src/roboseq.c && build/host/roboseq build/host/play tests/roboseq/play.txt'
	$(PY) -c "import glob; from PIL import Image; [Image.open(p).resize((480, 480), Image.NEAREST).save(p[:-4] + '.png') for p in glob.glob('build/host/play-*.ppm')]"
scenes: $(IMG)
	mkdir -p build/scenes
	$(RUN) sh -c 'cc -std=c11 -O1 -w -Ifirmware/src -Ifirmware/gen -Ifirmware/hal -o build/scenes/scenes tests/roboseq/scenes.c && build/scenes/scenes'
	$(PY) -c "import glob; from PIL import Image; [Image.open(p).resize((480, 480), Image.NEAREST).save(p[:-4] + '.png') for p in glob.glob('build/scenes/*.ppm')]"
probe:
	cd tools/tone-ref && npm ci --no-fund --no-audit
	node tools/tone-ref/probe_mix.mjs && python3 tools/tone-ref/compressor.py
fw:
	PYTHON=$(PY) sh build.sh
	shasum -a 256 build/roboseq.fwsc
check: fw $(IMG)
	$(RUN_SDK) sh tests/run_tests.sh
emu:             # (PLAY at 1 s: the default song; the panel at the end; the model and the ISR's load checked)
	mkdir -p build/emu/work
	cd build/emu && FM1_STRICT=1 FM1_DUMPRAM=ram.bin FM1_BENCH_KEYS='$(EMU_KEYS)' $(abspath $(EMU)) --fwsc ../roboseq.fwsc --shot $(EMU_MS) | tee emu.log
	sh tests/run_roboseq.sh >/dev/null
	$(PY) tools/emu_selfcheck.py build/roboseq.dis build/emu/ram.bin build/test/trace_test
	$(PY) -c "from PIL import Image; Image.open('build/emu/work/panel.ppm').save('build/emu/panel.png')"
	@echo "panel -> build/emu/panel.png"
image:
	@docker image inspect roboseq-dev >/dev/null 2>&1 || docker build -q --platform linux/amd64 -t roboseq-dev tools/docker
clean:
	rm -rf build
