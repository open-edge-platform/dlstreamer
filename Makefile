# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

.DEFAULT_GOAL := help
SHELL := /bin/bash

PROJECT_DIRECTORY 			:= ${CURDIR}
DLSTREAMER_INSTALL_PREFIX 	?= /opt/intel/dlstreamer
DEPENDENCY_DIR				:= build/deps
OPENVINO_DIR 				?= /opt/intel/openvino_2025

DLSTREAMER_VERSION 	:= 0.0.0
BUILD_TYPE 			?= Release
ENABLE_GENAI        := OFF

DOCKER_PRIVATE_REGISTRY := # Empty on purpose

LINUX_DISTRIBUTION := $(shell lsb_release -ds | cut -d " " -f1)
GENAI_DIR_SET := $(shell if [ -n "$$OpenVINOGenAI_DIR" ]; then echo true; else echo false; fi)
ifeq ($(LINUX_DISTRIBUTION), Ubuntu)
ifeq ($(GENAI_DIR_SET), true)
	ENABLE_GENAI := ON
endif
endif

export PATH 					:= ${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/install/bin:${PROJECT_DIRECTORY}/build/intel64/${BUILD_TYPE}/bin:${HOME}/.local/bin:${HOME}/python3venv/bin:${PATH}
export GST_PLUGIN_PATH 			:= ${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/gstreamer-bin/lib/gstreamer-1.0:${PROJECT_DIRECTORY}/build/intel64/${BUILD_TYPE}/lib:/usr/lib/x86_64-linux-gnu/gstreamer-1.0
export LIBRARY_PATH 			:= ${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/install/lib:${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/gstreamer-bin/lib:${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/opencv-bin/lib:${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/rdkafka-bin/lib:${PROJECT_DIRECTORY}/build/intel64/${BUILD_TYPE}/lib:/usr/lib
export PKG_CONFIG_PATH 			:= ${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/install/lib/pkgconfig:${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/gstreamer-bin/lib/pkgconfig:${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/rdkafka-bin/lib/pkgconfig:${PROJECT_DIRECTORY}/build/intel64/${BUILD_TYPE}/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig
export LIBVA_DRIVER_NAME 		:= iHD
export LIBVA_DRIVERS_PATH 		:= /usr/lib/x86_64-linux-gnu/dri
export GST_VA_ALL_DRIVERS 		:= 1
export GST_PLUGIN_FEATURE_RANK 	:= ${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX


.PHONY: dependencies
dependencies:
	@if [ ! -f build/deps/.deps_built ]; then \
		echo "Building dependencies..."; \
		cmake \
			-B build/deps \
			-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
			./dependencies; \
		cmake --build build/deps -j$(shell nproc); \
		touch build/deps/.deps_built; \
	else \
		echo "Dependencies already built, skipping..."; \
	fi

.PHONY: build
build: dependencies ## Compile Deep Learning Streamer
	cmake \
		-B build \
		-DCMAKE_PREFIX_PATH:PATH="${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/install;${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/opencv-bin;${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/rdkafka-bin" \
		-DCMAKE_INCLUDE_PATH:PATH=${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/install/include:${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/rdkafka-bin/include \
		-DCMAKE_LIBRARY_PATH:PATH=${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/install/lib \
		-DCMAKE_CXX_FLAGS="-I${PROJECT_DIRECTORY}/${DEPENDENCY_DIR}/rdkafka-bin/include" \
		-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
		-DENABLE_PAHO_INSTALLATION=ON \
		-DENABLE_RDKAFKA_INSTALLATION=ON \
		-DENABLE_VAAPI=ON \
		-DENABLE_SAMPLES=ON \
		-DENABLE_GENAI=${ENABLE_GENAI} \
		-DENABLE_TESTS=OFF; \
	cmake --build build -j$(shell nproc)

.PHONY: install
install: build ## Build and install Deep Learning Streamer
	@echo "Installing Deep Learning Streamer"
	@mkdir -p ${DLSTREAMER_INSTALL_PREFIX}
	@rm -rf ${DLSTREAMER_INSTALL_PREFIX}/*
	@if [ -f build/deps/.deps_built ]; then \
		echo "Installing dependencies..."; \
		cmake \
			-B build/deps \
			-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
			-DINSTALL_DLSTREAMER=True \
			-DDLSTREAMER_INSTALL_PREFIX=${DLSTREAMER_INSTALL_PREFIX} \
			./dependencies; \
	fi
	@cp -r build/intel64/${BUILD_TYPE} ${DLSTREAMER_INSTALL_PREFIX}
	@cp -r samples/ ${DLSTREAMER_INSTALL_PREFIX}
	@cp -r python/ ${DLSTREAMER_INSTALL_PREFIX}
	@cp -r scripts/ ${DLSTREAMER_INSTALL_PREFIX}
	@cp -r include/ ${DLSTREAMER_INSTALL_PREFIX}
	@cp README.md ${DLSTREAMER_INSTALL_PREFIX}
	@echo "Installation successful"

.PHONY: deb
deb: ## Build the Deep Learning Streamer DEB package for Ubuntu 24.04
	mkdir -p build/packages/deb
	docker build . \
		-f docker/ubuntu/ubuntu24.Dockerfile \
		-t deb-builder \
		--target deb-builder \
		--build-arg http_proxy=${http_proxy} \
		--build-arg https_proxy=${https_proxy} \
		--build-arg DLSTREAMER_VERSION=${DLSTREAMER_VERSION} \
		--build-arg DLSTREAMER_BUILD_NUMBER=1 \
		--build-arg DEV_MODE=true \
		--build-arg DOCKER_PRIVATE_REGISTRY=${DOCKER_PRIVATE_REGISTRY}
	docker create \
		--name deb-builder \
		deb-builder
	docker cp deb-builder:/intel-dlstreamer_${DLSTREAMER_VERSION}.1_amd64.deb ./build/packages/deb
	docker rm deb-builder

.PHONY: deb22
deb22: ## Build the Deep Learning Streamer DEB package for Ubuntu 22.04
	mkdir -p build/packages/deb
	docker build . \
		-f docker/ubuntu/ubuntu22.Dockerfile \
		-t deb-builder \
		--target deb-builder \
		--build-arg http_proxy=${http_proxy} \
		--build-arg https_proxy=${https_proxy} \
		--build-arg DLSTREAMER_VERSION=${DLSTREAMER_VERSION} \
		--build-arg DLSTREAMER_BUILD_NUMBER=1 \
		--build-arg DEV_MODE=true \
		--build-arg DOCKER_PRIVATE_REGISTRY=${DOCKER_PRIVATE_REGISTRY}
	docker create \
		--name deb-builder \
		deb-builder
	docker cp deb-builder:/intel-dlstreamer_${DLSTREAMER_VERSION}.1_amd64.deb ./build/packages/deb
	docker rm deb-builder

.PHONY: rpm
rpm: ## Build the Deep Learning Streamer RPM package
	mkdir -p build/packages/rpm
	docker build . \
		-t rpm-builder \
		-f docker/fedora41/fedora41.Dockerfile \
		--target rpm-builder \
		--build-arg http_proxy=${http_proxy} \
		--build-arg https_proxy=${https_proxy} \
		--build-arg DLSTREAMER_VERSION=${DLSTREAMER_VERSION} \
		--build-arg DLSTREAMER_BUILD_NUMBER=1 \
		--build-arg DEV_MODE=true \
		--build-arg DOCKER_PRIVATE_REGISTRY=${DOCKER_PRIVATE_REGISTRY}
	docker create \
		--name rpm-builder \
		rpm-builder
	docker cp rpm-builder:/intel-dlstreamer-${DLSTREAMER_VERSION}.1-1.fc41.x86_64.rpm ./build/packages/rpm
	docker rm rpm-builder

.PHONY: image
image: ## Build the Deep Learning Streamer docker image based on Ubuntu 24.04
	docker build . \
		-f docker/ubuntu/ubuntu24.Dockerfile \
		-t dlstreamer:dev \
		--target dlstreamer \
		--build-arg http_proxy=${http_proxy} \
		--build-arg https_proxy=${https_proxy} \
		--build-arg DLSTREAMER_VERSION=${DLSTREAMER_VERSION} \
		--build-arg DLSTREAMER_BUILD_NUMBER=1 \
		--build-arg DEV_MODE=true \
		--build-arg DOCKER_PRIVATE_REGISTRY=${DOCKER_PRIVATE_REGISTRY}

.PHONY: image22
image22: ## Build the Deep Learning Streamer docker image based on Ubuntu 22.04
	docker build . \
		-f docker/ubuntu/ubuntu22.Dockerfile \
		-t dlstreamer:dev \
		--target dlstreamer \
		--build-arg http_proxy=${http_proxy} \
		--build-arg https_proxy=${https_proxy} \
		--build-arg DLSTREAMER_VERSION=${DLSTREAMER_VERSION} \
		--build-arg DLSTREAMER_BUILD_NUMBER=1 \
		--build-arg DEV_MODE=true \
		--build-arg DOCKER_PRIVATE_REGISTRY=${DOCKER_PRIVATE_REGISTRY}

.PHONY: clean
clean: ## Cleanup any build artifacts
	@rm -rf build

.PHONY: help
help: ## Display help about the commands
	@grep -E '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'
