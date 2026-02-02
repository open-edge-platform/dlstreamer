# Coding Style

The Clang formatter is part of the CI setup. You need to ensure proper
code formatting before your commits are merged to `main`.
You can install the checker locally and configure your IDE to check
your code with it. You can also use a configuration file for ``clang-format-7``
from the root of this project (`.clang-format`).

## Install clang formatter

<!--hide_directive::::{tab-set}
:::{tab-item}hide_directive--> Ubuntu
<!--hide_directive:sync: tab1hide_directive-->

  ```bash
  sudo apt install -y clang-format-12
  sudo ln -s /usr/bin/clang-format-12 /usr/bin/clang-format
  ```

<!--hide_directive:::
:::{tab-item}hide_directive--> Other distros
<!--hide_directive:sync: tab2hide_directive-->

  You can find an appropriate repository at <https://apt.llvm.org/>.
  And install the newest version:

  ```bash
  sudo apt install -y clang-format-9
  sudo ln -s /usr/bin/clang-format-9 /usr/bin/clang-format
  ```

<!--hide_directive:::
::::hide_directive-->

## Configure IDE

- **Visual Studio Code**

  If you are using VS Code, you can simply install
  the `Clang-Format` extension and follow the provided
  instructions to configure VS Code to automatically format on save.

- **CLion**

  - Go to **File → Settings → Tools → External Tools** and click on the
    plus sign. A window should pop up. Choose a name, for example
    "clang-format"
  - For the Tool settings tab use this configuration:
    - **Program**: clang-format (you should use the name of your
      executable here)
    - **Parameters**: `-style=file -i FileName`
    - **Working directory**: `FileDir` Now, with your file open, you
      can go to **Tools → External** tools and run the config above.
      It basically calls `clang-format` and does in-place formatting
      using the style define in the first `.clang-format` file
      found in a parent directory.
- **Other IDEs**

  For different IDEs or text editors, refer to
  their respective documentation. If you would like to contribute
  instructions for your preferred IDE or editor, suggest an update for
  this guide. Your contribution is very welcome.
