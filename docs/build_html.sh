#!/bin/bash
# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

IMAGE_NAME=${1:-"sphinx-docs:latest"}
CONTAINER_NAME=${2:-docs_pages}
DOCKER_PRIVATE_REGISTRY=${3:-""}

ROOT="$(realpath "$(dirname "${0}")"/..)"
DOCS_DIR=$ROOT/docs
DOXYGEN_DIR=$ROOT/docs/user-guide/_doxygen
IMAGE_DOCS_DIR=/root/docs

# Copy necessary files located outside of this folder
echo "::group::Prepare for docker build"
cp -r "$ROOT"/samples/model_index.yaml "$DOCS_DIR"
cp -r "$ROOT"/samples/verified_models.json "$DOCS_DIR"
mkdir -p "$DOXYGEN_DIR"/src
cp -r "$ROOT"/gst-libs/gst/videoanalytics "$ROOT"/python/gstgva "$ROOT"/include/dlstreamer/gst/metadata "$DOXYGEN_DIR"/src
mkdir -p "$DOXYGEN_DIR"/src-api2.0
cp -r "$ROOT"/include/dlstreamer "$DOXYGEN_DIR"/src-api2.0
echo "::endgroup::"

# Build docker image
echo "::group::Building docker image"
docker_build="docker build \
  -f $DOCS_DIR/_docker/Dockerfile \
  -t $IMAGE_NAME \
  --build-arg DOCKER_PRIVATE_REGISTRY=$DOCKER_PRIVATE_REGISTRY \
  --build-arg DOCS_DIR=$IMAGE_DOCS_DIR \
  $DOCS_DIR"
echo "$docker_build"
if ! $docker_build ; then
  echo "::error::docker build failed"
  exit 1
fi
echo "::endgroup::"

echo "::group::Removing container"
docker container rm "$CONTAINER_NAME"
echo "::endgroup::"

# Building
echo "Building..."
BUILD_TYPES="html,spelling,linkcheck"
docker run --name "$CONTAINER_NAME" "$IMAGE_NAME" ./scripts/sphinx_build.sh "$BUILD_TYPES"
RUN_RESULT=$?

echo "::group::Gathering build result"
IFS=',' read -ra BUILDS <<< "$BUILD_TYPES"
for btype in "${BUILDS[@]}"; do
  build_dir="build-$btype"
  rm -rf "$build_dir"
  # Copy
  docker cp "$CONTAINER_NAME":$IMAGE_DOCS_DIR/"$build_dir" "$DOCS_DIR"
  # Remove non-relevant things
  rm -rf "$DOCS_DIR"/"$build_dir"/.buildinfo "$DOCS_DIR"/"$build_dir"/.doctrees/
done
echo "::endgroup::"

echo "Done, exit code $RUN_RESULT"
exit $RUN_RESULT