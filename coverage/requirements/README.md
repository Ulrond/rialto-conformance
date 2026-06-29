<!-- Copyright 2026 RDK Management — SPDX-License-Identifier: Apache-2.0 -->

# Requirements feed (mount point)

This directory is a **mount point for the private requirements feed**. It is
gitignored in this public repo exactly like `framework/`: nothing under it is
committed here except this README.

The public conformance record (`../matrix.yaml`) cites only **suite-owned,
source-neutral requirement ids** (`RC-*`). Those ids state what the Rialto
external interface must do; they say nothing about where the requirement came
from. The mapping from each `RC-*` id to its upstream certification programme —
the provenance, the partner catalogues, and the access classification of each —
lives **only in the private feed**, never in this repo.

## Populating the mount

The feed is a separate, access-gated repository. Clone or check it out into this
directory so the L4 ingestion tooling can read it:

```bash
# <private-feed-repo> is the access-gated requirements repository
git clone <private-feed-repo> coverage/requirements
```

Once mounted, this directory provides:

- `crosswalk.yaml` — maps each public `RC-*` id to its upstream catalogue ref(s).
- `catalogs/*.yaml` — the per-programme requirement refs
  (`{ ref, title, surface, codec/keysys, since }`).

Only the **media** requirements that map to Rialto's external surface are
catalogued — not app-level UI/network requirements Rialto does not own.
Confidential catalogues are referenced by ref id, not reproduced.

The public suite builds and runs without the mount; the feed gates only the L4
ingestion work that grows `../matrix.yaml`.
