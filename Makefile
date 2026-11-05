# RoboSeq: the everyday commands. The firmware itself is built by ./build.sh (docs/DINGHY_README.md: the toolchain).
#   make test    the model against the HTML's traces, then the app on the host (that in Docker on macOS: Dinghy's
#                host mode needs ELF sections); seconds, no FM-1
#   make traces  re-make tests/golden/traces from reference/roboseq.html (node)
#   make reference  re-render tests/golden/html with Tone.Offline (node, a Chromium)
#   make parity  the C voices against those renders
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
EMU_MS ?= 3000
EMU_KEYS ?= 1500:21,2600:21^,2800:25

.PHONY: test host traces reference parity fw check emu image clean
test: $(IMG)
	sh tests/run_roboseq.sh
	$(RUN) sh tests/run_host.sh
host: test
traces:
	node tools/trace/gen.mjs
reference:       ## re-render tests/golden/html from the HTML's own buildAudio() (node, a Chromium: Brave here)
	cd tools/tone-ref && npm ci --no-fund --no-audit && node render.mjs
parity:          ## the C voices against those renders, voice by voice (numpy)
	sh tests/run_roboseq.sh >/dev/null
	python3 tools/tone-ref/compare.py --check
fw:
	PYTHON=$(PY) sh build.sh
	shasum -a 256 build/roboseq.fwsc
check: fw $(IMG)
	$(RUN_SDK) sh tests/run_tests.sh
emu:             # (a key held from 1.5 s, another from 2.8 s; the panel at the end)
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
