# Final Requirements Compliance Audit

Used as the **last step before reporting completion** in
[`convert-app.prompt.md`](../../../prompts/convert-app.prompt.md).

The agent MUST walk through the checklist below and verify that every
requirement has been fulfilled. For each item, confirm with a concrete
artifact or test result — not just "I think I did this". If any item fails,
fix it and re-verify before proceeding.

The agent MUST emit the filled checklist (with `[x]` for pass, `[ ]` for
fail, and a one-line note per item) in the final user-facing summary message.
Any remaining `[ ]` items block completion.

## Deliverables

- [ ] `CMakeLists.txt` exists and builds without errors (`cmake --build` exit 0).
- [ ] Source files (`.cpp` / `.h`) exist and compile cleanly.
- [ ] `README.md` exists and is fully compliant with
      [`documentation-spec.md`](./documentation-spec.md) §1 (required
      sections) and §3 (Conversion Notes sub-sections).
- [ ] `run.sh` exists, is executable (`-x`), and meets every requirement in
      [`cmake-and-deliverables.md`](./cmake-and-deliverables.md) §"4. `run.sh`"
      (incl. `--help`, `--sink display|file|fake` selector + invalid-value
      error, headless auto-detection, pre-flight resource audit) AND wires
      the full env-setup recipe from
      [`conversion-bootstrap.md`](./conversion-bootstrap.md) §4.1 verbatim
      with `gst-inspect-1.0 OK:` for every GVA element the pipeline uses
      (§4.2).
- [ ] `export_models.sh` (if applicable) exists, is executable, and downloads
      models successfully when the models target directory is empty.

## Resource integrity

- [ ] Default input video path resolves to an existing file (no broken symlinks).
- [ ] All model files (`*.xml` + `*.bin`) exist at the default models path.
- [ ] Any label files, dictionaries, or configs referenced at runtime exist.

## Clean-shell runs (from [`validation-protocol.md`](./validation-protocol.md) §3a)

- [ ] `env -i … ./run.sh --help` → exit 0, usage printed.
- [ ] `env -i … ./run.sh` (defaults) → exit 0, non-zero FPS.
- [ ] `env -i … ./run.sh --sink fake` → exit 0, non-zero FPS.
- [ ] `env -i … ./run.sh --sink file` → exit 0, output file created.
- [ ] `env -i … ./run.sh --sink invalid` → exit non-zero, error message.

## Deprecated API compliance

- [ ] Dynamic deprecation scan returns empty (no deprecated constructs in
      output). See [`validation-protocol.md`](./validation-protocol.md) §6.

## Functional coverage

- [ ] Every functional block from the source-app inventory has a **dedicated,
      separate** counterpart element in the converted pipeline. No stages have
      been merged or dropped — the converted pipeline has the same number of
      inference stages as the original.
- [ ] The visual output of the converted app matches the source in kind: same
      categories of bounding boxes, labels, overlays, and counters are
      rendered (though model accuracy may differ). For example, if the
      original draws vehicle bounding boxes AND plate bounding boxes, the
      converted app MUST also draw both.

## Documentation completeness

See [`documentation-spec.md`](./documentation-spec.md) for the full README
specification. Confirm with one checkbox:

- [ ] README is fully compliant with `documentation-spec.md` §1 (required
      sections), §2 (Pipeline Comparison diagram), §3 (Conversion Notes
      sub-sections, incl. model substitutions table with
      `Character set / language` and `Domain match` columns where
      applicable), and §4 (Observed Output for all five clean-shell
      invocations). Source traceability comments
      (`/* --- Ref: … --- */`) are present in every logically distinct C++
      section per
      [`pipeline-implementation.md`](./pipeline-implementation.md) §9.
