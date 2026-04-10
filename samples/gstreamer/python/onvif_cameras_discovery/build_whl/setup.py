from pathlib import Path

from setuptools import setup

ROOT_DIR = Path(__file__).resolve().parent
SOURCE_DIR = ROOT_DIR.parent


def _read_requirements() -> list[str]:
    requirements_file = SOURCE_DIR / "requirements.txt"
    if not requirements_file.exists():
        return []

    requirements: list[str] = []
    for line in requirements_file.read_text(encoding="utf-8").splitlines():
        requirement = line.strip()
        if requirement and not requirement.startswith("#"):
            requirements.append(requirement)

    return requirements


def _read_long_description() -> str:
    readme_file = SOURCE_DIR / "README.md"
    if not readme_file.exists():
        return "ONVIF cameras discovery utilities for DLStreamer."

    return readme_file.read_text(encoding="utf-8")


setup(
    name="onvif-cameras-discovery",
    version="0.1.0",
    description="ONVIF cameras discovery utilities for DLStreamer",
    long_description=_read_long_description(),
    long_description_content_type="text/markdown",
    license="MIT",
    author="Intel Corporation",
    author_email="dlstreamer@intel.com",
    url="https://github.com/open-edge-platform/dlstreamer",
    python_requires=">=3.10",
    package_dir={"": ".."},
    py_modules=[
        "dls_onvif_camera_entry",
        "dls_onvif_config_manager",
        "dls_onvif_data",
        "dls_onvif_discovery_thread",
        "dls_onvif_discovery_engine",
        "dls_onvif_sample",
        "misc",
    ],
    install_requires=_read_requirements(),
    data_files=[
        (
            "share/onvif_cameras_discovery",
            ["../config.json", "../dls_onvif_sample.jpg"],
        )
    ],
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Topic :: Multimedia :: Video",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
        "Topic :: System :: Networking",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Operating System :: POSIX :: Linux",
    ],
)
