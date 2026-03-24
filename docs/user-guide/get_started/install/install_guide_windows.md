# Install Guide Windows

This page describes steps required to install Deep Learning Streamer Pipeline
Framework on Windows.

## On Windows System

### Step 1: Download and extract DL Streamer DLL files

Download the archive from
[DL Streamer assets on GitHub](https://github.com/open-edge-platform/edge-ai-libraries/releases)
Extract to a new folder, for example `C:\\dlstreamer_dlls`.

### Step 2: Run setup script

Open a PowerShell prompt as and administrator, run the following script and follow instructions:

```bash
cd C:\\dlstreamer_dlls
.\setup_dls_env.ps1
```

> **NOTE:** There can be an execution policy on your system which does not allow to execute
> PowerShell scripts. If you encounter such situation, policy can be securely changed in open
> terminal with the following PowerShell command: `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`

You are ready to use Deep Learning Streamer. For further instructions to run
sample pipeline(s), please go to the [tutorial](../tutorial.md)
There is need to manually download models.

------------------------------------------------------------------------

> **\*** *Other names and brands may be claimed as the property of
> others.*
