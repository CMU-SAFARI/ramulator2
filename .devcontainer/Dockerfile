FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG USERNAME=vscode

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        clangd \
        clang-format \
        clang-tidy \
        cmake \
        curl \
        gdb \
        git \
        lld \
        lldb \
        python-is-python3 \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        ripgrep \
        sudo \
    && rm -rf /var/lib/apt/lists/*

RUN if ! getent group "${USERNAME}" >/dev/null; then groupadd "${USERNAME}"; fi \
    && if ! id -u "${USERNAME}" >/dev/null 2>&1; then useradd --gid "${USERNAME}" --create-home --shell /bin/bash "${USERNAME}"; fi \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
    && chmod 0440 "/etc/sudoers.d/${USERNAME}" \
    && echo '[ -n "$VIRTUAL_ENV" ] && . "$VIRTUAL_ENV/bin/activate"' >> /home/${USERNAME}/.bashrc
