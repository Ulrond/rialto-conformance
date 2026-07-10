#!/usr/bin/env bash
#
# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# Software-render self-check for the Linux software platform (issue #18).
#
# The RialtoServer decodes and renders through a GStreamer `playbin`. On a SoC the
# platform supplies real audio/video sinks; on the Linux software platform there
# is no display and no audio device, and `playbin` leaves audio-sink/video-sink
# unset so it falls to autoaudiosink/autovideosink — which try real sinks and fail
# headlessly. GST_PLUGIN_FEATURE_RANK promotes the fake sinks so autodetect selects
# them, and gstreamer1.0-libav supplies avdec_h264/avdec_aac for real decode.
#
# This asserts, before the conformance gate runs, that the image can actually
# decode H.264 + AAC to EOS headlessly through those fake sinks. If it cannot, the
# software render path is broken and the data-path conformance cases cannot run, so
# this fails loudly rather than letting them mis-report.
set -uo pipefail

# Headless: no display, no audio device. Force autodetect onto the fake sinks.
unset DISPLAY XDG_RUNTIME_DIR
export GST_PLUGIN_FEATURE_RANK="fakevideosink:MAX,fakeaudiosink:MAX${GST_PLUGIN_FEATURE_RANK:+,${GST_PLUGIN_FEATURE_RANK}}"

fail=0
check() {  # check <label> <gst-launch args...>
    local label="$1"; shift
    local out; out="$(gst-launch-1.0 -e "$@" 2>&1)"; local rc=$?
    if [ $rc -eq 0 ] && grep -qi "Got EOS" <<<"$out" \
       && ! grep -qiE "error|cannot|could not|not-negotiated|no such element" <<<"$out"; then
        echo "[render-check] PASS  ${label}"
    else
        echo "[render-check] FAIL  ${label} (rc=${rc})" >&2
        grep -iE "error|cannot|could not|not-negotiated|no such element" <<<"$out" | head -3 | sed 's/^/[render-check]       /' >&2
        fail=1
    fi
}

# Real decode (encode -> parse -> decode -> autodetect sink) to EOS, headless. The
# autodetect sinks mirror what the RialtoServer playbin selects.
check "H.264 decode -> autovideosink(fake)" \
    videotestsrc num-buffers=15 ! video/x-raw,width=320,height=240 ! x264enc ! h264parse ! avdec_h264 ! autovideosink
check "AAC decode -> autoaudiosink(fake)" \
    audiotestsrc num-buffers=15 ! avenc_aac ! aacparse ! avdec_aac ! autoaudiosink

if [ $fail -eq 0 ]; then
    echo "[render-check] software render path OK (real H.264 + AAC decode to EOS, headless)"
else
    echo "[render-check] ERROR: software render path is broken" >&2
fi
exit $fail
