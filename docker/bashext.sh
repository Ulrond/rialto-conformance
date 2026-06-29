# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# Minimal bash extension sourced by the SC entrypoint / `sc docker run` (which
# does `source /usr/local/bin/bashext.sh`). Kept intentionally small and generic.

# Prepend a directory to PATH, de-duplicating.
path_prepend()
{
    for arg in "$@"; do
        case ":${PATH}:" in
            *":${arg}:"*) : ;;
            *) export PATH="${arg}:${PATH}" ;;
        esac
    done
}

# repo_flow / sc live here on SC dev hosts; harmless if absent.
[ -d /opt/repo_flow ] && path_prepend /opt/repo_flow

# Note: the suite's runtime library paths (libut_control, the locally-built
# libRialtoClient, the rialtomse*sink plugin via GST_PLUGIN_PATH) are set by the
# caller relative to the mounted repo — see sc-run.sh. They are not hardcoded here
# because the repo mount path varies per host.
