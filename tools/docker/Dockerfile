# Linux for what macOS cannot do here: Dinghy's host mode (ELF sections). `make image` builds it (as fm1apsis's).
FROM debian:bookworm-slim
RUN apt-get update -qq && apt-get install -y -qq gcc clang lld make python3 python3-pil git >/dev/null \
 && rm -rf /var/lib/apt/lists/*
