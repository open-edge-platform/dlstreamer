# Documentations

## Project documentation

Project documentation source lives under `user-guide/` as Markdown (MyST) files,
built with Sphinx. This repository does not check in `conf.py`/`Makefile` -
the build configuration and theme are pulled at build time from a shared
documentation template, the same way the `Documentation Check` CI workflow
(`.github/workflows/documentation-check.yaml`) does it.

Follow the steps below to build the docs locally. Run all commands from the
**repository root** (`dlstreamer/`), not from inside `docs/` - the downloaded
Makefile references paths such as `docs/user-guide/` relative to the repo
root, so running it from within `docs/` fails with a "master document" error.

1. From the repository root, download and extract the shared template
   (provides `conf.py`, `Makefile`, and `dict.txt`):

    ```shell
        wget https://docs.openedgeplatform.intel.com/template/template.tar.gz
        tar xf template.tar.gz
    ```

2. Build the HTML documentation:

    ```shell
        make build
    ```
    To see the built documentation, open the generated `index.html` file
    under the build output directory (e.g. `out/html/index.html`).

3. Run the spelling check:

    ```shell
        make sphinx-spelling
    ```
    Words known to be spelled correctly but missing from the dictionary can be
    added to `spelling_wordlist.txt` (see `user-guide/spelling_wordlist.txt`).
    Each `.md` page is accompanied by a `.spelling` report file with the list
    of misspelled words and their location.

4. Run the link check:

    ```shell
        make sphinx-linkcheck
    ```
    Two report files are generated: `output.json` with the complete list of
    URLs checked, and `output.txt` with the list of broken links.

> **NOTE:** The exact `make` targets and output paths are defined by the
> downloaded template and may change; run `make help` after extracting the
> template to see the targets available in your checkout.


