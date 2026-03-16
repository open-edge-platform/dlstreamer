# Documentations

## API documentation (Doxygen)

To see Doxygen's API documentation open `doxygen/index.md` file.

## Project documentation

To see project documentation you need to build `.rst` source files with Sphinx tool.

Follow the steps below:

1. Install sphinx tool, e.g. with `pip`:

    ```shell
        pip3 install sphinx m2r2 sphinx_book_theme sphinxcontrib-mermaid sphinxcontrib-spelling
    ```

2. Build `.rst` pages in the format you need, e.g. `html`:

    ```shell
        sphinx-build -b html ./user-guide ./build-html
    ```
    If Running Sphinx shows error like below:
    ```
    Exception occurred:
        File "/usr/lib/python3/dist-packages/jinja2/loaders.py", line 163, in __init__
            self.searchpath = list(searchpath)
        TypeError: 'PosixPath' object is not iterable
    ```
    Update Jinja2 to latest version:
    ```shell
    pip install -U Jinja2
    ```
    To see `html` built documentation open `./build-html/index.html` file.

3. Run spelling check of `.rst` pages:
    Update conf.py adding `sphinxcontrib.spelling` to
    ```
    extensions = ... , 'sphinxcontrib.mermaid', 'sphinxcontrib.spelling']
    ```

    Dictionary configuration can be done setting up

    ```
    #Dictionary selected
    spelling_lang='en_US'

    #Path of file containing a list of words (one word per line) known to be spelled correctly but that
    #do not appear in the language dictionary selected
    spelling_word_list_filename=<your_path>'/spelling_wordlist.txt'

    #Enable suggestions for misspelled words
    spelling_show_suggestions=True
    ```

    ```shell
        sphinx-build -b spelling ./user-guide ./build-spelling
    ```
    Each `.rst` page is accompained by a `.spelling` report file with the list of misspelled words and the location.

4. Run link check of `.rst` pages:

    ```shell
        sphinx-build -b linkcheck ./user-guide ./build-linkcheck
    ```
    Two reports file are generated: `output.json` with the complete list of URL checked; `output.txt` with the list of broken links.


