# Deprecated APIs — DO NOT USE

Used by **step 2** of [`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

The agent MUST NOT introduce any DL Streamer or GStreamer API/element/file format
that the official documentation marks as **deprecated**, **legacy**,
**discontinued**, **obsolete**, or **"will be removed"**. This applies to new
code generated during conversion regardless of whether the original app used a
now-deprecated mechanism — the conversion is the opportunity to modernize, not
to mirror legacy choices.

Before using ANY element, property, file format, or metadata API, the agent
MUST discover the current set of deprecations directly from the upstream
documentation — never from a hard-coded list (which would go stale).

## Discovery procedure

1. **Scan the docs and skills for deprecation notices** at the start of every
   conversion run:

   ```bash
   grep -rniB1 -A3 --include='*.md' \
       -E '\b(deprecat|discontinu|obsolete|legacy|will be removed|no longer supported|end[- ]of[- ]life)\b' \
       <workspace_root>/dlstreamer/docs \
       <workspace_root>/dlstreamer/.github/skills \
     | grep -viE 'CHANGELOG|third[-_]party|node_modules'
   ```

2. **Build a per-run deprecation table** from the grep output. For each hit
   extract:
   - the deprecated symbol/element/file/API name,
   - the source file + line,
   - (from the surrounding paragraph) the documented replacement.

3. **Treat that table as authoritative** for the current conversion. The agent
   MUST re-run this discovery on every `/convert-app` invocation; do not cache
   results from a previous run.

4. **Forbid every discovered item in newly-generated code.** Pick the documented
   replacement from the same paragraph the deprecation notice lives in. If the
   replacement is not stated explicitly, follow the link the deprecation notice
   points to and read it to determine the correct modern API.

5. **Record the resulting table in the README** under
   `Conversion Notes → Deprecation discovery (run NNN)` so the user can audit
   which version of the docs the conversion was made against.

## Enforcement

1. If a deprecated API is the **only** technically viable path for a given
   conversion (extremely rare), the agent MUST:
   - Document the constraint in the README under
     `Conversion Notes → Deprecated API usage justification`, naming the exact
     API, the deprecation source file + line found in step 1, the replacement
     that was attempted, and the concrete reason it failed.
   - Add a `TODO:` comment in the source code at the call site referencing that
     README section.
   - Surface the constraint in the user-facing summary message at the end of
     the run.

2. When converting a source application that itself relies on a now-deprecated
   DL Streamer pattern (e.g. an old sample shipped with a `model_proc/*.json`),
   the agent MUST modernize it during the port — do NOT carry the legacy
   artifact forward "because the upstream sample uses it".

## Final scan (validation)

The final dynamic-grep scan that blocks the conversion if any deprecated
construct survives in `<output_dir>/` is specified in
[`validation-protocol.md`](./validation-protocol.md) §6. The agent runs that
scan during step 6, using the per-run table built above as the regex source.
