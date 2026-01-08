#!/bin/bash
# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

npu_driver_version='1.23.0'
reinstall_npu_driver='no'  # Default value for reinstalling the NPU driver
on_host_or_docker='host'

# Show help message
show_help() {
    cat <<EOF

Usage: $(basename "$0") [OPTIONS]

Script for installing and configuring prerequisites for DL Streamer

Options:
  -h, --help                                                  Show this help message and exit
  --reinstall-npu-driver=[yes|no]                             (Re)install Intel® NPU driver (default: no)
  --on-host-or-docker=[host|docker_ubuntu22|docker_ubuntu24]  Script execution on host or in Docker container (default: host)

Examples:
  $(basename "$0")
  $(basename "$0") --reinstall-npu-driver=yes
  $(basename "$0") --reinstall-npu-driver=yes --on-host-or-docker=docker_ubuntu24
  $(basename "$0") --help

EOF
}

# Parse command-line arguments
for i in "$@"; do
    case $i in
        -h|--help)
            show_help
            exit 0
        ;;
        --reinstall-npu-driver=*)
            reinstall_npu_driver="${i#*=}"
            shift
        ;;
        --on-host-or-docker=*)
            on_host_or_docker="${i#*=}"
            shift
        ;;
        *)
            echo "Unknown option: $i"
            show_help
            exit 1
        ;;
    esac
done

echo "NPU driver version set to: $npu_driver_version"
echo "Reinstall NPU driver: $reinstall_npu_driver"
echo "Running on: $on_host_or_docker"

# Define the Intel® repository URL, keyring path, and key URL

INTEL_CL_GPU_KEY_URL="https://repositories.intel.com/gpu/intel-graphics.key"
INTEL_CL_GPU_REPO_URL_22="https://repositories.intel.com/gpu/ubuntu jammy unified"
INTEL_CL_GPU_REPO_URL_24="ppa:kobuk-team/intel-graphics"

INTEL_GPU_KEYRING_PATH="/usr/share/keyrings/intel-graphics.gpg"

INTEL_GPU_LIST_22="intel-gpu-jammy.list"
INTEL_GPU_LIST_24="kobuk-team-ubuntu-intel-graphics-noble.sources"

CURL_TIMEOUT=60
APT_UPDATE_TIMEOUT=60
APT_GET_TIMEOUT=600

# ***********************************************************************
# Function to display text in a given color
echo_color() {
    local text="$1"
    local color="$2"
    local color_code=""

    # Determine the color code based on the color name
    case "$color" in
        black) color_code="\e[30m" ;;
        red) color_code="\e[31m" ;;
        green) color_code="\e[32m" ;;
        bred) color_code="\e[91m" ;;
        bgreen) color_code="\e[92m" ;;
        yellow) color_code="\e[33m" ;;
        blue) color_code="\e[34m" ;;
        magenta) color_code="\e[35m" ;;
        cyan) color_code="\e[36m" ;;
        white) color_code="\e[37m" ;;
        *) echo "Invalid color name"; echo "$color" ;;
    esac

    # Display the text in the chosen color
    echo -e "${color_code}${text}\e[0m"
}

# Function to handle errors
handle_error() {
    echo -e "\e[31mError occurred: $1\e[0m"
    exit 1
}

# ***********************************************************************
# Function to download the Intel® repository GPG key with a timeout
configure_repository() {
    local key_url="$1"
    local new_keyring_path="$2"
    local repo_url="$3"
    local list_name="$4"

    echo "Setting up $repo_url..."

    # Find all files containing the repository entry
    local repo_files
    repo_files=$(grep -rl "^deb .*$repo_url" /etc/apt/sources.list /etc/apt/sources.list.d/)


    # Loop through each file and check for signed-by entries
    for repo_file in $repo_files; do
        # Extract the keyring path from the matching line
        local keyring_path
        keyring_path=$(grep "^deb .*$repo_url" "$repo_file" | grep -oP 'signed-by=\K[^ ]+' | sed 's/]$//' | head -n 1)

        # Check if a keyring path was found
        if [ -n "$keyring_path" ]; then
            echo "Found repository signed with keyring: $keyring_path in $repo_file"

            # Prompt the user for action
            read -p "Do you want to apply the new signature? [y/N] " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                echo "Skipping the application of the new signature."
                return 0
            fi
        fi
    done

    # Check if the new keyring file already exists
    if [ -f "$new_keyring_path" ]; then
        # Generate a new keyring file name
        local base_keyring_name
        base_keyring_name=$(basename "$new_keyring_path")
        local keyring_dir
        keyring_dir=$(dirname "$new_keyring_path")
        local new_keyring_name
        new_keyring_name="${base_keyring_name%.*}_$(date +%s).gpg"
        new_keyring_path="$keyring_dir/$new_keyring_name"
        echo "Keyring file already exists. Using new keyring file: $new_keyring_path"
    fi

    echo "Downloading the GPG key..."
    if ! curl -fsSL --connect-timeout "$CURL_TIMEOUT" "$key_url" | $SUDO_PREFIX gpg --yes --dearmor --output "$new_keyring_path"; then
        echo_color "\n Failed to download the GPG key due to a network issue. Exiting the script." "bred"
        exit 1
    fi

    echo "Signing the repository..."
    echo "deb [arch=amd64 signed-by=$new_keyring_path] $repo_url" | $SUDO_PREFIX tee "/etc/apt/sources.list.d/$list_name" > /dev/null

}

# ***********************************************************************
# Function to update the package lists with a timeout
update_package_lists() {

    # Update the package lists with a timeout and capture the output
    local update_output
    update_output=$(timeout --foreground $APT_UPDATE_TIMEOUT $SUDO_PREFIX apt-get update 2>&1)
    local update_exit_code=$?

    # Display the output
    echo "$update_output"

    # Check for a timeout
    if [ $update_exit_code -eq 124 ]; then
        echo_color "The update process timed out." "bred"
        exit 1
    fi

    # Check for specific error messages in the output
    if echo "$update_output" | grep -q "403  Forbidden"; then
        echo_color " Repository access was forbidden. Check your repository configuration." "bred"
        exit 1
    elif echo "$update_output" | grep -q "is not signed"; then
        echo_color " One of the repositories is not signed. Check your repository configuration." "bred"
        exit 1
    elif [ $update_exit_code -ne 0 ]; then
        echo_color " Failed to update package lists for an unknown reason." "bred"
        exit 1
    else
        echo_color " Package lists updated successfully." "green"
    fi
}
# ***********************************************************************

install_packages() {
    # Define a temporary file for logging
    local log_file
    log_file=$(mktemp)

    echo_color " Installing packages: $*." "yellow"

    # Run apt-get install and use tee to duplicate the output to the log file
    # while still displaying it to the user
    timeout --foreground $APT_GET_TIMEOUT $SUDO_PREFIX apt-get install -y --allow-downgrades "$@" 2>&1 | tee "$log_file"
    local status=${PIPESTATUS[0]}

    # Check the exit status of the apt-get install command
    if [[ $status -eq 124 ]]; then
        handle_error "The command timed out."
    elif [ "$status" -ne 0 ]; then
        echo_color " An error occurred during package installation." "bred"

        # Check for common errors and suggest solutions
        if grep -qi "Unable to fetch some archives" "$log_file"; then
            echo_color " Your package lists may be outdated. Try running '$SUDO_PREFIX apt-get update' and then re-run this script." "bred"
        elif grep -qi "Unable to locate package" "$log_file"; then
            echo_color " One or more specified packages could not be found. Check for typos or availability in your current software sources." "yellow"
        elif grep -qi "dpkg was interrupted" "$log_file"; then
            echo_color " The package installation was previously interrupted. Try running '$SUDO_PREFIX dpkg --configure -a' to fix this." "yellow"
        else
            echo_color " Check the error messages in the log above to determine the cause of the installation failure." "yellow"
        fi

        # Clean up the log file
        rm -f "$log_file"

        # Exit the script with an error status
        exit 1
    else
        message=" Installed $* successfully."
        echo_color "$message" "green"
    fi

    # Clean up the log file
    rm -f "$log_file"
}

# Function to check if the kernel version is 6.12 or higher
check_kernel_version() {
    # Get the current kernel version
    current_kernel=$(uname -r)

    # Extract the major and minor version numbers
    current_major=$(echo "$current_kernel" | cut -d. -f1)
    current_minor=$(echo "$current_kernel" | cut -d. -f2)

    # Define the target major and minor version numbers
    target_major=6
    target_minor=12

    # Function to compare version numbers
    version_ge() {
        [ "$1" -gt "$2" ] || { [ "$1" -eq "$2" ] && [ "$3" -ge "$4" ]; }
    }

    # Check if the current kernel version is greater than or equal to the target version
    if version_ge "$current_major" "$target_major" "$current_minor" "$target_minor"; then
        return 0  # Success
    else
        return 1  # Failure
    fi
}

# Create a temporary directory for downloading .deb files
temp_dir=$(mktemp -d)
echo " Created temporary directory: $temp_dir"

# Cleanup function to remove the temporary directory
cleanup() {
    echo " Cleaning up temporary files..."
    rm -rf "$temp_dir"
}

# Trap to execute the cleanup function on script exit or interrupt
trap cleanup EXIT

# Function to download a package
download_deb_package() {
    local package_url=$1
    local timeout_value=$2

    # Extract the package name from the URL
    local package_name
    package_name=$(basename "$package_url")

    # Download the package with the specified timeout
    echo "Downloading $package_name..."
    wget --timeout="$timeout_value" -O "$package_name" "$package_url"

    # Check if the download was successful
    # shellcheck disable=SC2181
    if [ $? -eq 0 ]; then
        echo_color " Downloaded $package_name successfully." "green"
    else
        handle_error " Failed to download $package_name."
    fi
}

# Function to download and install a package
install_deb_package() {
    local package_url=$1
    local timeout_value=$2

    # Extract the package name from the URL
    local package_name
    package_name=$(basename "$package_url")

    if ! $SUDO_PREFIX -E apt install -y --allow-downgrades ./"$package_name"; then
        echo_color " Attempting to fix broken dependencies..." "yellow"
        $SUDO_PREFIX apt --fix-broken install || handle_error "Failed to fix broken dependencies"
        # Try the installation again after fixing dependencies
        $SUDO_PREFIX apt install -y ./"$package_name" || handle_error "apt failed to install $package_name after fixing dependencies"
    else
        echo_color " Installed $package_name successfully. \n" "green"
    fi
}

# This function configures the Intel® Client GPU based on the Ubuntu version
setup_gpu(){
    local ubuntu_version="${1:-$(lsb_release -rs)}"
    case $intel_gpu_state in
        1)
            echo_color "\n ✓ Intel® Client GPU detected! We'll automatically install the optimized GPU drivers for your system. \n" "green"
            ;;
        2)
            echo_color "\n Your system contains unsupported Intel® Data Center GPU. To get more information about drivers, please visit: https://dgpu-docs.intel.com/driver/installation.html#ubuntu" "bred"
            exit 1
            ;;
    esac
    $SUDO_PREFIX apt update
    # Additional packages for Ubuntu 22.04/24.04
    if [ "$ubuntu_version" == "24.04" ]; then
        $SUDO_PREFIX apt-get install -y --no-install-recommends software-properties-common
        $SUDO_PREFIX add-apt-repository -y $INTEL_CL_GPU_REPO_URL
        $SUDO_PREFIX apt update
        echo "Snapshot: 20251125T030400Z" | $SUDO_PREFIX tee -a /etc/apt/sources.list.d/$INTEL_GPU_LIST
        $SUDO_PREFIX apt update
        install_packages intel-metrics-discovery intel-gsc libvpl2 \
            libze-intel-gpu1=25.40.35563.7-1~24.04~ppa1 libze1=1.24.3-1~24.04~ppa1 intel-opencl-icd=25.40.35563.7-1~24.04~ppa1 clinfo=3.0.23.01.25-1build1 \
            intel-media-va-driver-non-free=25.4.2-1~24.04~ppa1 libmfx-gen1=25.4.0-0ubuntu1~24.04~ppa1 libvpl-tools=1.4.0-0ubuntu1~24.04~ppa1 libva-glx2=2.22.0-1ubuntu1~24.04~ppa1 va-driver-all=2.22.0-1ubuntu1~24.04~ppa1 vainfo=2.22.0-0ubuntu1~24.04~ppa1
    elif [ "$ubuntu_version" == "22.04" ]; then
        configure_repository "$INTEL_CL_GPU_KEY_URL" "$INTEL_GPU_KEYRING_PATH" "$INTEL_CL_GPU_REPO_URL" "$INTEL_GPU_LIST"
        install_packages clinfo libze-intel-gpu1=25.18.33578.15-1146~22.04 libze1=1.21.9.0-1136~22.04 intel-media-va-driver-non-free=25.2.4-1146~22.04 intel-opencl-icd=25.18.33578.15-1146~22.04
    fi
}
# Function to get .deb package URLs from the latest release of a GitHub repository
get_deb_urls() {
    local REPO=$1

    # Get the latest release information using the GitHub API
    local LATEST_RELEASE
    LATEST_RELEASE=$(curl -s "https://api.github.com/repos/$REPO/releases/latest")

    # Extract the URLs of the .deb packages
    local DEB_URLS
    DEB_URLS=$(echo "$LATEST_RELEASE" | grep browser_download_url | grep -Eo 'https://[^"]+\.deb' | sed 's/%2B/+/g')

    # Return the list of .deb package URLs
    echo "$DEB_URLS"
}

# Function to get the URLs of .deb packages from a GitHub release page
get_deb_urls_no_api() {
  local repo="$1"
  local tag="$2"
  local filter="$3"
  local release_url="https://github.com/$repo/releases/tag/$tag"
  local expanded_assets_url
  local deb_urls

  # Fetch the HTML content of the release page
  html_content=$(curl -s "$release_url")

  # Extract the URL of the expanded assets
  expanded_assets_url=$(echo "$html_content" | grep -oP 'include-fragment[^>]+src="\K[^"]+' | grep 'expanded_assets')

  # Check if the expanded_assets_url starts with "https"
  if [[ "$expanded_assets_url" != https* ]]; then
    expanded_assets_url="https://github.com$expanded_assets_url"
  fi

  # Fetch the HTML content of the expanded assets page
  expanded_assets_content=$(curl -s "$expanded_assets_url")

  # Extract the URLs of .deb files
  #deb_urls=$(echo "$expanded_assets_content" | grep -oP 'href="\K[^"]+\.deb' | sed 's|^|https://github.com|')

  if [ -n "$filter" ]; then
    deb_urls=$(echo "$expanded_assets_content" | grep -oP 'href="\K[^"]+\.deb' | grep "$filter" | grep -v "devel" | sed 's|^|https://github.com|')
  else
    deb_urls=$(echo "$expanded_assets_content" | grep -oP 'href="\K[^"]+\.deb' | sed 's|^|https://github.com|')
  fi

  echo "$deb_urls"
}

# Function to get the URLs of .deb packages from a GitHub release page
get_deb_urls_wget() {
  local repo="$1"
  local tag="$2"
  local filter="$3"
  local release_url="https://github.com/$repo/releases/tag/$tag"
  local deb_urls

  # Fetch the HTML content of the release page
  html_content=$(curl -s "$release_url")

  # Extract the URLs of .deb files from lines starting with wget
  if [ -n "$filter" ]; then
    deb_urls=$(echo "$html_content" | grep -oP 'wget \Khttps://[^ ]+\.deb' | grep "$filter")
  else
    deb_urls=$(echo "$html_content" | grep -oP 'wget \Khttps://[^ ]+\.deb')
  fi

  echo "$deb_urls"
}

# Function to check if the current user is a member of a given group and add if not
add_user_to_group_if_not_member() {
  local group="$1"
  local user="$USER"

  # Check if the group exists
  if ! getent group "$group" > /dev/null; then
    handle_error " Group '$group' does not exist."
  fi

  # Check if the user is a member of the group
  if id -nG "$user" | tr ' ' '\n' | grep -q "^$group$"; then
    echo_color " The user $user is already a member of the group $group." "green"
    return 0
  else
    # Add the user to the group
    $SUDO_PREFIX usermod -aG "$group" "$user"
    # shellcheck disable=SC2181
    if [ $? -eq 0 ]; then
      echo_color " The user $user has been added to the group $group." "cyan"
      return 1
    else
      handle_error  " Failed to add the user $user to the group $group."
    fi
  fi
}

# Function to get the installed version of a package
get_installed_version() {
  local package_name="$1"
  dpkg-query -W -f='${Version}' "$package_name" 2>/dev/null
}

# Function to check the version of a package
check_package_version() {
  local package_name="$1"

  # Check if the package is installed
  if dpkg -l "$package_name" 2>/dev/null | grep -q "^ii"; then
    # Get the package version
    package_version=$(dpkg -l "$package_name" | grep "^ii" | awk '{print $3}')
    echo "The version of the package '$package_name' is: $package_version"
  else
    echo "The package '$package_name' is not installed."
  fi
}

# Function to get the latest version of a package from GitHub by following the redirect
get_latest_version_github() {

  local repo="$1"
  local latest_url="https://github.com/$repo/releases/latest"
  local redirect_url

  # Use curl to follow the redirect and extract the final URL
  redirect_url=$(curl -s -L -o /dev/null -w '%{url_effective}' "$latest_url")

  # Extract the version tag from the final URL
  latest_version=$(basename "$redirect_url")

  # Remove the leading "v" if present
  #latest_version="${latest_version#v}"

  echo "$latest_version"
}


setup_npu() {

    # Change to the temporary directory
    pushd "$temp_dir" > /dev/null || handle_error "Failed to change to temporary directory"

    update_package_lists
    install_packages libtbb12

    REPO1="intel/linux-npu-driver"
    REPO2="oneapi-src/level-zero"

    TAG1=$(get_latest_version_github "$REPO1")
    TAG2=$(get_latest_version_github "$REPO2")

    FILTER1="$ubuntu_version"

    # Get .deb package URLs for each repository
    DEB_URLS1=$(get_deb_urls_no_api "$REPO1" "$TAG1" "$FILTER1")
    DEB_URLS2=$(get_deb_urls_no_api "$REPO2" "$TAG2" "$FILTER1")

    # Merge the results into a single array
    package_urls=()
    while IFS= read -r url; do
        package_urls+=("$url")
    done <<< "$DEB_URLS1"

    while IFS= read -r url; do
        package_urls+=("$url")
    done <<< "$DEB_URLS2"

    # Iterate over the list of package URLs and download each one
    for package_url in "${package_urls[@]}"; do
        download_deb_package "$package_url" "$APT_GET_TIMEOUT"
        install_deb_package "$package_url" "$APT_GET_TIMEOUT"
    done


    # Return to the original directory
    popd > /dev/null || handle_error "Failed to return to original directory"
}

install_npu() {
    local ubuntu_version="${1:-$(lsb_release -rs)}"
    $SUDO_PREFIX rm -rf ./npu_debs
    mkdir -p ./npu_debs && cd npu_debs || exit
    dpkg --purge --force-remove-reinstreq intel-driver-compiler-npu intel-fw-npu intel-level-zero-npu
    if [ "$ubuntu_version" == "22.04" ]; then
        wget https://github.com/oneapi-src/level-zero/releases/download/v1.22.4/level-zero_1.22.4+u22.04_amd64.deb
        wget https://github.com/intel/linux-npu-driver/releases/download/v1.23.0/linux-npu-driver-v1.23.0.20250827-17270089246-ubuntu2204.tar.gz
        tar -xf linux-npu-driver-v1.23.0.20250827-17270089246-ubuntu2204.tar.gz
    elif [ "$ubuntu_version" == "24.04" ]; then
        wget https://github.com/oneapi-src/level-zero/releases/download/v1.22.4/level-zero_1.22.4+u24.04_amd64.deb
        wget https://github.com/intel/linux-npu-driver/releases/download/v1.23.0/linux-npu-driver-v1.23.0.20250827-17270089246-ubuntu2404.tar.gz
        tar -xf linux-npu-driver-v1.23.0.20250827-17270089246-ubuntu2404.tar.gz
    fi
    $SUDO_PREFIX apt update
    $SUDO_PREFIX apt install libtbb12
    $SUDO_PREFIX  $SUDO_PREFIX dpkg -i *.deb
    cd ..
    rm -rf ./npu_debs
    $SUDO_PREFIX apt-get clean
    $SUDO_PREFIX rm -rf /var/lib/apt/lists/*
    $SUDO_PREFIX rm -f /etc/ssl/certs/Intel*
}

# ***********************************************************************
if [ "$on_host_or_docker" == "host" ]; then
    need_to_reboot=0
    need_to_logout=0

    # Detect Ubuntu version
    ubuntu_version=$(lsb_release -rs)

    # Get the CPU family and model information
    cpu_family=$(grep -m 1 'cpu family' /proc/cpuinfo | awk '{print $4}')
    cpu_model=$(grep -m 1 'model' /proc/cpuinfo | awk '{print $3}')
    cpu_model_name=$(lscpu | grep "Model name:" | awk -F: '{print $2}' | xargs)

    echo_color "\n CPU is Intel Family $cpu_family Model $cpu_model ($cpu_model_name).\n" "yellow"

    # Choose the package list based on the Ubuntu version
    case "$ubuntu_version" in
        24.04)
            echo_color " Detected Ubuntu version: $ubuntu_version. " "green"
            INTEL_CL_GPU_REPO_URL=$INTEL_CL_GPU_REPO_URL_24
            INTEL_GPU_LIST=$INTEL_GPU_LIST_24
            ;;
        22.04)
            echo_color " Detected Ubuntu version: $ubuntu_version. " "green"
            INTEL_CL_GPU_REPO_URL=$INTEL_CL_GPU_REPO_URL_22
            INTEL_GPU_LIST=$INTEL_GPU_LIST_22
            ;;
        *)
            echo_color " Unsupported Ubuntu version: $ubuntu_version. Exiting." "red"
            exit 1
            ;;
    esac


    # Check if the CPU is Intel Family 6 Model 189 (Lunar Lake)
    if [[ "$cpu_family" == "6" && "$cpu_model" == "189" ]]; then

        check_kernel_version
        status=$?

        if [ $status -eq 0 ]; then
            echo_color " Kernel 6.12 or higher detected." "green"
        else
            echo_color "\n WARNING!" "red"
            echo_color "\n Deep Learning Streamer on Lunar Lake family processors has only been tested with the 6.12 kernel. We strongly recommend updating the kernel version before proceeding." "red"
            read -p " Quit installation? [y/n] " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                exit
            fi
        fi
    fi
fi

#-----------------------STEP 1-------------------------------------------
#Check if script is running on host or in docker

if [ "$on_host_or_docker" == "docker_ubuntu22" ] || [ "$on_host_or_docker" == "docker_ubuntu24" ]; then
    update_package_lists
    install_packages curl wget gpg software-properties-common

    SUDO_PREFIX=""
    intel_gpu_state=1
    DRI_DIR="/dev/dri"
    package_name="intel-driver-compiler-npu"

    case "$on_host_or_docker" in
        docker_ubuntu22)
            INTEL_CL_GPU_REPO_URL=$INTEL_CL_GPU_REPO_URL_22
            INTEL_GPU_LIST=$INTEL_GPU_LIST_22
            ubuntu_version="22.04"
            ;;
        docker_ubuntu24)
            INTEL_CL_GPU_REPO_URL=$INTEL_CL_GPU_REPO_URL_24
            INTEL_GPU_LIST=$INTEL_GPU_LIST_24
            ubuntu_version="24.04"
            ;;
    esac

    setup_gpu "$ubuntu_version"
    update_package_lists

    #-----------CHECK IF GPU DRIVERS ARE INSTALLED-------------------------------
    # Check for the presence of renderD* devices
    render_devices=""
    for file in "$DRI_DIR"/renderD[0-9]*; do
        echo "Checking file: $file"
        if [[ -e "$file" && $(basename "$file") =~ ^renderD[0-9]+$ ]]; then
            echo "Adding $file to render_devices"
            render_devices+="$file "
        fi
    done

    # Trim the trailing space if any files were found
    render_devices=${render_devices% }

    if [ -n "$render_devices" ]; then
        echo_color "\n GPU drivers have been installed.\n" "green"
        intel_gpu_driver_state=1
    fi

    # Install NPU and check version
    install_npu "$ubuntu_version"
    installed_version=$(get_installed_version "$package_name")
    echo "Installed version of $package_name is $installed_version"
    exit 0
else
    SUDO_PREFIX="sudo"
fi


#-----------------------STEP 2-------------------------------------------

# Inform the user about the upcoming package installation
echo_color "\n The script will now update package lists and install required packages." "yellow"

$SUDO_PREFIX rm -rf /usr/share/keyrings/intel*gpg
$SUDO_PREFIX rm -rf /etc/apt/sources.list.d/intel*
$SUDO_PREFIX apt --fix-broken install -y
$SUDO_PREFIX rm -rf /var/cache/apt/archives/*.deb
update_package_lists
install_packages curl wget jq gpg pciutils software-properties-common

#-----------------------INSTALL GPU DRIVERS------------------------------

# Initialize the GPU state variable
# 0 - No Intel® GPU
# 1 - Intel® client GPU
# 2 - Intel® Data Center GPU
intel_gpu_state=0
intel_gpu_driver_state=0
gpu_info=""

# Check for any Intel® GPU
intel_gpus=$(lspci -nn | grep -E 'VGA|3D|Display' | grep -i "Intel")

# If Intel® GPUs are found, prioritize DC GPU
if [ -n "$intel_gpus" ]; then
    # Check for DC GPU
    intel_dc_gpu=$(echo -e "$intel_gpus" | grep -Ei "0BD5|0BDA|56C0|56C1")
    if [ -n "$intel_dc_gpu" ]; then
        intel_gpu_state=2
        gpu_info="$intel_dc_gpu"
        echo_color "\n Your system contains unsupported Intel® Data Center GPU. To get more information about drivers, please visit: https://dgpu-docs.intel.com/driver/installation.html#ubuntu" "bred"
        exit 1
    else
        # If no DC GPU is found, it must be a client GPU
        intel_client_gpu=$(echo -e "$intel_gpus")
        if [ -n "$intel_client_gpu" ]; then
            intel_gpu_state=1
            gpu_info="$intel_client_gpu"
        fi
    fi
fi

# Output the results based on the GPU state
case $intel_gpu_state in
    0)
        echo_color "\n No Intel® GPU detected." "bred"
        ;;
    1)
        echo_color "\n Intel® Client GPU detected:" "bgreen"
        echo -e " --------------------------------"
        echo -e " $gpu_info"
        echo -e " --------------------------------"
        ;;
    2)
        echo_color "\n Intel® Data Center GPU detected:" "bgreen"
        echo -e " -----------------------------------"
        echo -e " $gpu_info"
        echo -e " -----------------------------------"
        ;;
esac


if [ $intel_gpu_state -ne 0 ]; then

    setup_gpu

    #-----------CHECK IF GPU DRIVERS ARE INSTALLED-------------------------------

    # Path to the /dev/dri directory
    DRI_DIR="/dev/dri"
    echo "Contents of $DRI_DIR:"
    ls -l "$DRI_DIR"
    # Check for the presence of renderD* devices
    render_devices=""
    for file in "$DRI_DIR"/renderD[0-9]*; do
        echo "Checking file: $file"
        if [[ -e "$file" && $(basename "$file") =~ ^renderD[0-9]+$ ]]; then
            echo "Adding $file to render_devices"
            render_devices+="$file "
        fi
    done

    # Trim the trailing space if any files were found
    render_devices=${render_devices% }

    if [ -n "$render_devices" ]; then
        echo_color "\n GPU drivers have been installed.\n" "green"
        intel_gpu_driver_state=1
    fi


    #----------------------ADD USERS TO GROUPS---------------------------------
    echo_color " Adding user to video and render groups..." "yellow"

    add_user_to_group_if_not_member "render"
    # shellcheck disable=SC2181
    if [ $? -ne 0 ]; then
    need_to_logout=1
    fi

    add_user_to_group_if_not_member "video"
    # shellcheck disable=SC2181
    if [ $? -ne 0 ]; then
    need_to_logout=1
    fi
    #$SUDO_PREFIX usermod -a -G video "$USER" || handle_error "Failed to add user to video group."
    #$SUDO_PREFIX usermod -a -G render "$USER" || handle_error "Failed to add user to render group."
fi

update_package_lists

#-------------------------STEP 3-------------------------------------------
#-----------CHECK IF SYSTEM CONTAINS NPU-------------------------------

if $SUDO_PREFIX dmesg | grep -q 'intel_vpu'
then
    # In this case we know that NPU must be present in the system, so we can proceed with the installation
    echo_color " This system contains a Neural Processing Unit." "green"

    repo="intel/linux-npu-driver"  # Replace with the GitHub repository in the format "owner/repo"
    package_name="intel-driver-compiler-npu"

    latest_version=$(get_latest_version_github "$repo")
    latest_version="${latest_version#v}" # Remove the leading "v" if present
    installed_version=$(get_installed_version "$package_name")
    installed_version=$(echo "$installed_version" | grep -oP '^\d+\.\d+\.\d+') # This extracts the version number in the format X.Y.Z

    if [ -z "$installed_version" ]; then
        echo "The package '$package_name' is not installed."
    else
        echo "Latest version of '$package_name' from GitHub: $latest_version"
        echo "DLStreamer is tested on $npu_driver_version. Checking if installed version of $package_name is $npu_driver_version."

        if [ "$npu_driver_version" == "$installed_version" ]; then
            echo "The installed version is $installed_version. "

            intel_npu=$(lspci | grep -i 'Intel' | grep 'NPU' | rev | cut -d':' -f1 | rev)

            if [ -z "$intel_npu" ]; then
                intel_npu="Intel® NPU"
            fi

            line_to_add="export ZE_ENABLE_ALT_DRIVERS=libze_intel_npu.so"

            # Define the .bash_profile file path for the current user
            bash_profile="${HOME}/.bash_profile"

            # Check if .bash_profile exists, create it if it does not
            if [ ! -f "$bash_profile" ]; then
                # If .bash_profile does not exist, check for .profile
                if [ ! -f "${HOME}/.profile" ]; then
                    # Neither .bash_profile nor .profile exists, create .bash_profile
                    touch "$bash_profile"
                else
                    # .profile exists, so use that instead
                    bash_profile="${HOME}/.profile"
                fi
            fi

            # Check if the line already exists in .bash_profile to avoid duplicates
            if ! grep -qF -- "$line_to_add" "$bash_profile"; then
                # If the line does not exist, append it to .bash_profile
                echo "$line_to_add" >> "$bash_profile"
                # shellcheck disable=SC1090
                source "$bash_profile"
            fi
        else
            echo "The installed version is not $npu_driver_version, installed version is $installed_version"
        fi
    fi

    if [[ "$reinstall_npu_driver" =~ ^[Yy][Ee][Ss]$ ]]; then
        #setup_npu
        install_npu
        need_to_reboot=1
        installed_version=$(get_installed_version "$package_name")
        installed_version=$(echo "$installed_version" | grep -oP '^\d+\.\d+\.\d+')

    elif [[ -z "$installed_version" || "$npu_driver_version" != "$installed_version" ]]; then
        echo_color "The installation of the NPU driver was skipped." "bred"
        echo_color "It is recommended to install version $npu_driver_version. You can rerun the script with --reinstall-npu-driver=yes to have the script reinstall the driver for you." "bred"
        echo_color "Reboot after installation will be required." "bred"
    else
        echo_color "Intel NPU driver is in $installed_version version and ready to use." "bgreen"
    fi
fi

if [ "$need_to_reboot" -eq 1 ]; then
  echo_color "\n A reboot is required!." "bred"

  # Ask the user for permission to reboot
  read -p " Do you want to reboot now? (y/n): " -r answer

  # Check the user's response
  if [[ "$answer" =~ ^[Yy]$ ]]; then
    echo_color " Remember to rerun the script after the reboot." "cyan"
    echo " Rebooting the system..."
    $SUDO_PREFIX reboot
  else
    echo " Reboot canceled."
    exit
  fi
else
    echo_color "\n Environment setup completed successfully. " "bgreen"
    echo_color " You may now proceed with the installation of DL Streamer." "green"

    echo " ---------------------------------------------------"
    echo  " The following hardware will be enabled: "
    echo  " - CPU ($cpu_model_name) "

    if [ $intel_gpu_driver_state -ne 0 ]; then
        short_gpu_info=$(echo "$gpu_info" | grep -o "Intel.*")
        echo " - GPU ($short_gpu_info)"
    fi

    if [ -n "$intel_npu" ]; then
        echo " - NPU ($intel_npu)"
    fi

    echo " ---------------------------------------------------"

    if [ -z "$render_devices" ]; then
        echo_color "\n Intel® GPU hardware is present but drivers could not be installed." "yellow"

        case $intel_gpu_state in
        1)
            echo_color " To enable GPU support, install the Intel® Client GPU drivers manually by following the instructions available at https://dgpu-docs.intel.com/driver/client/overview.html \n" "cyan"
            ;;
        2)
            echo_color " Your system contains unsupported Intel® Data Center GPU. To get more information about drivers, please visit: https://dgpu-docs.intel.com/driver/installation.html#ubuntu \n" "cyan"
            ;;
        esac

    fi

    if [ "$need_to_logout" -eq 1 ]; then
        echo_color "User added to render and video groups. Please log out and log back in for the changes to take effect." "cyan"
    fi
fi
