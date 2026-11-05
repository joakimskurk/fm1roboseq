# RoboSeq: the everyday commands. The firmware itself is built by ./build.sh (docs/DINGHY_README.md: the toolchain).
#   make test    the host tests (on macOS in Docker: Dinghy's host mode needs ELF sections), seconds, no FM-1
#   make fw      the FM-1 package build/roboseq.fwsc (JieLi toolchain in ~/.jieli, SDK in ~/fw-AC79_AIoT_SDK)
#   make check   everything CI runs: the package, then Dinghy's platform tests on it
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

.PHONY: test host fw check image clean
test: $(IMG)
	$(RUN) sh tests/run_host.sh
host: test
fw:
	PYTHON=$(PY) sh build.sh
	shasum -a 256 build/roboseq.fwsc
check: fw $(IMG)
	$(RUN_SDK) sh tests/run_tests.sh
image:
	@docker image inspect roboseq-dev >/dev/null 2>&1 || docker build -q --platform linux/amd64 -t roboseq-dev tools/docker
clean:
	rm -rf build
