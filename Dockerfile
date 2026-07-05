# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# Linux software-platform BUILD ENVIRONMENT for rialto-conformance.
#
# Linux is treated as just another platform: the backend underneath is a SOFTWARE
# Rialto, not a SoC. This image is the toolchain + Rialto's native build deps +
# the SC-compatible entrypoint, so the suite can be built and run against a
# software Rialto with no hardware and no host root.
#
# It is a build ENVIRONMENT, not a baked build: the repo is mounted at run time
# (by `sc docker run`, which also maps you as the in-container user) and the build
# runs then. Drive it with the helper:
#
#     ./sc-run.sh                 # ensure sc + image, then build + run the CORE gate
#
# or directly (own base; no internal registry/cert — SC's run-time user mapping
# is provided by the entrypoint below, so `sc docker run` works against it):
#
#     docker build -t rialto-conformance-env .
#     sc docker run -l rialto-conformance-env -- \
#         "./install.sh && ./build-rialto.sh --no-deps && ./build.sh && \
#          RIALTO_CONFORMANCE_TIER=core ./build/bin/rialto_conformance -a -p profiles/deviceConfig.linux.yaml"

ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}

LABEL org.opencontainers.image.title="rialto-conformance-env" \
      org.opencontainers.image.description="Linux software-platform build environment for the Rialto conformance suite" \
      org.opencontainers.image.source="https://github.com/Ulrond/rialto-conformance" \
      org.opencontainers.image.licenses="Apache-2.0"

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Europe/London

# Toolchain + Rialto's native build deps (Rialto's own list) + GStreamer runtime
# to actually decode and render. wget is needed by ut-control's configure.sh.
#
# The GStreamer runtime split matters for the software render path (issue #18):
#   -plugins-base  : appsrc, decodebin/playbin, autodetect sinks, videotestsrc
#   -plugins-good  : aacparse and friends
#   -plugins-bad   : h264parse (videoparsersbad) + fakevideosink
#   -plugins-ugly  : x264enc — synthesize real-codec test clips (pairs with #22)
#   -libav         : avdec_h264 / avdec_aac / avenc_aac — the actual software
#                    decoders, so the RialtoServer playbin really decodes rather
#                    than stalling on a missing decoder
#   -tools         : gst-launch-1.0 / gst-inspect-1.0 for asset synthesis + probes
# The RialtoServer's playbin leaves audio-sink/video-sink unset, so autodetect
# degrades to fakesink when the container has no display/audio device — decode
# runs headlessly to EOS with the clock advancing.
RUN apt-get update && apt-get install -y --no-install-recommends \
        git ca-certificates wget curl gnupg sudo locales \
        build-essential cmake pkg-config \
        unzip zip patch autoconf automake libtool m4 \
        python3 python3-pip python3-venv \
        protobuf-compiler libprotobuf-dev \
        libunwind-dev libyaml-cpp-dev \
        libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
        gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
        gstreamer1.0-tools \
    && rm -rf /var/lib/apt/lists/*

# gosu, for the SC entrypoint's privilege drop to the mapped user.
RUN curl -fsSL "https://github.com/tianon/gosu/releases/download/1.19/gosu-$(dpkg --print-architecture)" \
        -o /usr/local/bin/gosu \
    && chmod 0755 /usr/local/bin/gosu

# SC-compatible helpers so `sc docker run` maps the caller as the in-container
# user (files in the mounted workspace stay owned by you, not root).
COPY docker/entrypoint.sh docker/bashext.sh /usr/local/bin/
RUN chmod 0755 /usr/local/bin/entrypoint.sh /usr/local/bin/bashext.sh && mkdir -p /etc/entrypoint.d

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["echo 'rialto-conformance build env. Run via ./sc-run.sh or: sc docker run -l <img> -- \"<cmds>\"'"]
