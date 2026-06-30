# Conversion Bootstrap — Source Acquisition & Environment Setup

Used by **step 1** of [`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

## 1. Workspace root resolution

The prompt MUST be runnable end-to-end starting from an empty workspace. The agent
never assumes any prior state.

- **Workspace root** = the parent directory of the `dlstreamer` repository in the
  user's open workspace (i.e. `dirname(<workspace>/dlstreamer)`).
- All checkout / output happens directly under this root, **never inside
  `dlstreamer/`**.

## 2. Idempotent source clone

If the user provides a remote URL (e.g. GitHub repository):

```bash
if [ ! -d "<workspace_root>/<source_repo_name>" ]; then
    git clone <URL> "<workspace_root>/<source_repo_name>"
fi
```

- If the directory exists, **reuse it** — do not re-clone, do not delete.
- Record the source URL and resolved commit hash (`git rev-parse HEAD`) in the
  README so the conversion is reproducible.

## 3. Fresh numbered output directory (per invocation)

Every invocation of `/convert-app` MUST create a **brand-new** output directory.
Previous conversions are never overwritten or modified.

Naming scheme:

- **Base name**: `<converted_app_name>` derived deterministically from the source
  (e.g. `deepstream_lpr_app` → `deepstream_lpr_app_dls`).
- **Suffix**: 3-digit iteration `_NNN` where `NNN` is the smallest integer ≥ 1
  such that `<workspace_root>/<converted_app_name>_NNN/` does not yet exist.
  Examples: `deepstream_lpr_app_dls_001`, `deepstream_lpr_app_dls_002`, …

Procedure:

```bash
# Scan workspace root for existing numbered dirs, pick next index
max=$(ls -d <workspace_root>/<converted_app_name>_[0-9][0-9][0-9] 2>/dev/null \
      | sed -E 's/.*_([0-9]{3})$/\1/' | sort -n | tail -1)
next=$(printf "%03d" $(( ${max:-0} + 1 )))
mkdir -p "<workspace_root>/<converted_app_name>_${next}/"
```

- All deliverables go into this newly-created numbered directory **only**.
- Do not touch the previous numbered directories.
- **Report the chosen directory name to the user up-front**, e.g.
  > "Creating conversion #003 in `deepstream_lpr_app_dls_003/`"

## 4. DL Streamer runtime prerequisite

A working DL Streamer installation **with environment variables exported** is
required to build, run, and validate the converted application. Before starting
the conversion, the agent MUST follow the **mandatory env-setup recipe** below
in order. Skipping any step has historically caused the conversion to abort
mid-flight with `No such element 'gvadetect'`.

### 4.1 Mandatory env-setup recipe (apply in order)

Apply this recipe both in the **agent's own shell** (before running any
`gst-inspect-1.0` verification) AND verbatim in the generated `run.sh` (see
[cmake-and-deliverables.md](./cmake-and-deliverables.md) §4 and the *Known
`run.sh` pitfalls* table in [runsh-pitfalls.md](./runsh-pitfalls.md)).

```bash
# Step 1 — Detect install
if [[ ! -d /opt/intel/dlstreamer ]]; then
    echo "ERROR: DL Streamer not found at /opt/intel/dlstreamer." >&2
    # Direct user to the install guide and exit non-zero
    exit 1
fi

# Step 2 — Source the upstream env script with set -u relaxed
#   setup_dls_env.sh references unset vars (e.g. GST_PLUGIN_FEATURE_RANK) and
#   crashes under `set -u`. Wrap the source in set +u / set -u.
set +u
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
set -u

# Step 3 — Add Release/lib to GST_PLUGIN_PATH and LD_LIBRARY_PATH
#   setup_dls_env.sh does NOT add the Release/lib directory, but on most
#   installs that is exactly where libgstvideoanalytics.so (which provides
#   gvadetect/gvaclassify/gvawatermark/gvatrack) actually lives.
#   Without this, gst-inspect-1.0 gvadetect returns "No such element".
export GST_PLUGIN_PATH="/opt/intel/dlstreamer/Release/lib:${GST_PLUGIN_PATH:-}"
export LD_LIBRARY_PATH="/opt/intel/dlstreamer/opencv/lib:/opt/intel/dlstreamer/Release/lib:${LD_LIBRARY_PATH:-}"

# Step 4 — Deprioritize kmssink (avoids negotiation errors on remote/SSH/CI)
export GST_PLUGIN_FEATURE_RANK="kmssink:NONE,${GST_PLUGIN_FEATURE_RANK:-}"
```

**User-facing notice (mandatory when the agent applies §4.1 in its own shell
because the env was not already exported)** — the agent MUST explicitly inform
the user that the fix is per-session and tell them how to make it permanent:

> "DL Streamer was installed but its environment was not exported in this
> shell. I sourced `setup_dls_env.sh` (plus the `Release/lib` / `kmssink:NONE`
> additions above) for the current session. To make this permanent, add the
> same `source` line — followed by the `export GST_PLUGIN_PATH=…`,
> `export LD_LIBRARY_PATH=…`, and `export GST_PLUGIN_FEATURE_RANK=…` lines —
> to your `~/.bashrc` (or `~/.zshrc` on zsh)."

The generated `run.sh` applies the full recipe at every launch, so the user
does NOT need the shell-rc change to run the converted app — only for ad-hoc
`gst-inspect-1.0` / `gst-launch-1.0` commands or building other DL Streamer
apps in fresh shells.

### 4.2 Mandatory verification (the only acceptable evidence of success)

After applying the recipe, the agent MUST verify each of the GVA elements the
converted pipeline will actually use. Do NOT only check `gvadetect` — a partial
plugin load can leave `gvadetect` working while `gvaclassify` or `gvatrack` is
silently missing.

```bash
for e in gvadetect gvaclassify gvawatermark gvatrack gvafpscounter gvapython; do
    gst-inspect-1.0 "$e" >/dev/null 2>&1 && echo "OK: $e" || echo "MISSING: $e"
done
```

All required elements MUST report `OK:` before proceeding to step 2 of the
prompt. If any reports `MISSING:`, apply the recovery procedure in §4.3 below.

### 4.3 Registry-cache recovery (if verification still fails)

A corrupted `~/.cache/gstreamer-1.0/registry.x86_64.bin` can persist failed
discovery state across shells. Symptoms:

- `gst-inspect-1.0 gvadetect` returns "No such element" even after applying
  §4.1 in full.
- `gst-plugin-scanner` prints `CRITICAL **: Couldn't set __plugin__ attribute`
  or `TypeError: PyModule_AddObjectRef()` (a broken `libgstpython.so` aborted
  registry rebuild on a previous run, leaving a stale partial cache).
- `gst-inspect-1.0 --gst-plugin-load=/opt/intel/dlstreamer/Release/lib/libgstvideoanalytics.so gvadetect`
  **does** print the factory details (proves the `.so` itself is fine — only
  auto-discovery is broken).

Recovery:

```bash
# 1. Delete the corrupted registry cache
rm -rf ~/.cache/gstreamer-1.0

# 2. Re-apply the §4.1 recipe in a clean shell (env -i …) to ensure the
#    registry is rebuilt with the correct GST_PLUGIN_PATH on the first try
env -i HOME="$HOME" PATH="/usr/local/bin:/usr/bin:/bin" TERM="${TERM:-xterm}" \
    bash -lc '
        set +u; source /opt/intel/dlstreamer/scripts/setup_dls_env.sh; set -u
        export GST_PLUGIN_PATH="/opt/intel/dlstreamer/Release/lib:${GST_PLUGIN_PATH:-}"
        export LD_LIBRARY_PATH="/opt/intel/dlstreamer/opencv/lib:/opt/intel/dlstreamer/Release/lib:${LD_LIBRARY_PATH:-}"
        gst-inspect-1.0 gvadetect >/dev/null 2>&1 && echo OK || echo STILL_BROKEN
    '

# 3. If STILL_BROKEN — the libgstpython.so plugin shipped with DL Streamer is
#    incompatible with the system Python. Move it aside as a non-destructive
#    workaround and rebuild the registry. Inform the user of the move.
#    Only do this if the conversion does NOT rely on `gvapython`.
PY_PLUGIN=/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0/libgstpython.so
if [[ -f "$PY_PLUGIN" ]]; then
    sudo mv "$PY_PLUGIN" "${PY_PLUGIN}.disabled-by-agent"
    rm -rf ~/.cache/gstreamer-1.0
    # re-verify
fi
```

If §4.3 step 3 is reached, the agent MUST:

- Surface the change to the user (the inline quoted notice above, including
  the exact `sudo mv ... ${PY_PLUGIN}` restore command) AND record the
  workaround in the README under `Conversion Notes → Environment workarounds`.
- If the converted pipeline DOES need `gvapython`, do NOT apply step 3 —
  instead block the conversion and ask the user to fix the Python plugin
  (mismatch between DLS-shipped Python and system Python).

### 4.4 Install-from-scratch (only if `/opt/intel/dlstreamer` is missing)

Follow the official guide for the user's platform:

- [Installation Guide Index](../../../../docs/user-guide/get_started/install/install_guide_index.md)
- [Ubuntu](../../../../docs/user-guide/get_started/install/install_guide_ubuntu.md)
- [Ubuntu on WSL2](../../../../docs/user-guide/get_started/install/install_guide_ubuntu_wsl2.md)
- [Windows](../../../../docs/user-guide/get_started/install/install_guide_windows.md)

Then return to §4.1 and proceed.

### 4.5 Documentation requirement

The converted app's README MUST reproduce the install-guide reference(s) and
the §4.1 env-setup recipe under `Prerequisites`, and record any §4.3 step 3
workaround under `Conversion Notes → Environment workarounds`. See
[`documentation-spec.md`](./documentation-spec.md) §1 for the README layout.
