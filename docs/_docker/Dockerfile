# ==============================================================================
# Copyright (C) 2022 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

ARG DOCKER_PRIVATE_REGISTRY

FROM ${DOCKER_PRIVATE_REGISTRY}ubuntu:22.04

ARG DOCS_DIR=${HOME}

ENV DEBIAN_FRONTEND noninteractive

RUN apt update && \
    apt install -y -q --no-install-recommends \
    python3 python3-pip doxygen wget unzip xz-utils liblua5.2-dev graphviz git enchant-2 \
    && apt-get clean

# Install Sphinx and required extensions
RUN pip3 install --upgrade pip \
    && pip3 install sphinx==6.2.0 sphinx-book-theme sphinxcontrib-mermaid sphinxcontrib-spelling sphinx-copybutton sphinx-tabs myst-parser jsonschema

# Copy source
COPY . ${DOCS_DIR}
WORKDIR ${DOCS_DIR}

# Install Doxyrest
RUN wget https://github.com/vovkos/doxyrest/releases/download/doxyrest-2.1.3/doxyrest-2.1.3-linux-amd64.tar.xz -P ${HOME} \
    && mkdir -p ${HOME}/doxyrest \
    && tar -xf ${HOME}/doxyrest-2.1.3-linux-amd64.tar.xz -C ${HOME}/doxyrest --strip-components 1 \
    && cp -r ${HOME}/doxyrest/share/doxyrest/frame ${DOCS_DIR}/user-guide/_doxygen

# Build Doxygen docs and convert to rst
RUN cd ${DOCS_DIR}/user-guide/_doxygen && doxygen Doxyfile \
    && $HOME/doxyrest/bin/doxyrest -c ./doxyrest-config.lua \
    && cp GVA_API.png ../api_ref/

# Build Doxygen docs for API 2.0 and convert to rst
RUN cd ${DOCS_DIR}/user-guide/_doxygen && doxygen Doxyfile-api2.0 \
    && $HOME/doxyrest/bin/doxyrest -c ./doxyrest-config-api2.0.lua

# Download open_model_zoo and generate supported_models.rst
RUN cd ${HOME} && wget https://github.com/openvinotoolkit/open_model_zoo/archive/refs/heads/master.zip \
    && unzip master.zip && rm -f master.zip
RUN pip3 install PyYAML \
    && python3 ${DOCS_DIR}/scripts/models_table_from_yaml.py \
      --model_index=${DOCS_DIR}/scripts/all_models.yaml \
      --verified_models=${DOCS_DIR}/scripts/supported_models.json \
      --output=${DOCS_DIR}/user-guide/supported_models.rst

ENV DOXYREST_SPHINX_DIR=/root/doxyrest/share/doxyrest/sphinx/

#ENTRYPOINT ["/usr/local/bin/sphinx-build"]
#CMD ["-b", "html", "./user-guide", "./build"]
