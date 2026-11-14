# Linux-container build of native Windows x64 executables.
# Docker Desktop may stay in its default Linux-containers mode.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates \
       cmake \
       ninja-build \
       g++-mingw-w64-x86-64-posix \
       binutils-mingw-w64-x86-64 \
       file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
ENTRYPOINT ["/bin/bash", "/workspace/docker/build.sh"]
