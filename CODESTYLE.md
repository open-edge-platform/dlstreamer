# Code style

## Source Code Formatting

We are using formatting convention based on LLVM with few changes you can find in `.clang-format` file.

## How to use .clang-format

Install clang-format-7 then configure your IDE to use it and optionally you can add symlink to make your editor automatically take it by name:

```sh
sudo apt install -y clang-format-7
sudo ln -s /usr/bin/clang-format-7 /usr/bin/clang-format
```

Instruction for CLion:

- Go to File->Settings->Tools->External Tools and click on the plus sign. A window should pop up. Choose a name, for example "clang-format"
- For the Tool settings tab use this configuration:
  - Program: clang-format (you should use the name of your executable here)
  - Parameters: --style=file -i $FileName$
  - Working directory: $FileDir$
Now, with your file open, you can go to Tools->External tools and run the config above. It basically calls clang-format and does inplace formatting using the style define in the first .clang-format file found in a parent directory.


For VS Code use this extension [clang-format](https://marketplace.visualstudio.com/items?itemName=xaver.clang-format)

## Naming

This applies to C++ code. New and changed code follows it; existing code is not renamed in bulk.

| Kind | Style | Example |
| --- | --- | --- |
| Types | PascalCase | `InferSlotPool`, `PreparedModel` |
| Functions and methods | PascalCase | `PrepareModel()`, `GetBatchSize()` |
| Variables | snake_case | `batch_size`, `model_path` |
| Private and protected class members | snake_case_ | `current_slot_`, `requests_mutex_` |
| Public fields of plain structs | snake_case (no underscore) | `PreparedModel::image_input_name` |
| Constants | UPPER_CASE | `KEY_BATCH_SIZE` |
| Macros | UPPER_CASE | `ITT_TASK` |
| Namespaces | lower_case (new ones only) | `post_processing` |
| Files | snake_case | `infer_slot_pool.h` |

Rules of thumb:

- Spell acronyms as words (`Roi`, `Nireq`, `OpenVino`), for example `OpenVinoImageInference`. Existing names such as
  `OpenVINOContext` are not renamed in bulk.
- A method that overrides a virtual function keeps the name of the interface it implements. The same holds for names
  that an external interface requires, for example `fmt::formatter::format`.
- Existing namespaces such as `InferenceBackend` keep their names.
