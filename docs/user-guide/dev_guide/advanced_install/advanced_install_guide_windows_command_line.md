# Advanced Installation on Windows - Install via Command Line

The installer supports installing via command line (silent mode) for unattended installations.
Use the `/S` flag to run without the graphical interface.

## Command-Line Flags

| Flag | Description | Required | Default |
|---|---|---|---|
| `/S` | Run in silent mode | Yes | — |
| `/TYPE=<type>` | Set the installation type | No | `Typical` |
| `/COMPONENTS=<list>` | Comma-separated list of optional components | No | — |
| `/D=<path>` | Custom install directory | No | `%programfiles%\Intel\dlstreamer` |

## Installation Types

| Type | Optional Components Included |
|---|---|
| `Full` | `python`, `env`, `samples`, `development` |
| `Typical` | `python`, `env`, `samples` |
| `Minimal` | *(none)* |

> **Note:** Required components (GStreamer and Runtime) are always installed regardless of the type or components selected.

## Usage

```ps1
.\dlstreamer-<version>-win64.exe /S [/TYPE=<type>] [/COMPONENTS=<list>] [/D=<path>]
```

## Examples
**Install with default settings (Typical):**

```ps1
.\dlstreamer-<version>-win64.exe /S
```

**Install with a specific type:**

```ps1
.\dlstreamer-<version>-win64.exe /S /TYPE=Minimal
```

**Install with specific components:**

```ps1
.\dlstreamer-<version>-win64.exe /S /COMPONENTS=python,env,samples,development
```

**Custom install directory:**

```ps1
.\dlstreamer-<version>-win64.exe /S /D=C:\custom\path\dlstreamer
```

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
