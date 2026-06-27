FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

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

ARG NODE_MAJOR=22
RUN curl -fsSL https://deb.nodesource.com/setup_${NODE_MAJOR}.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

RUN existing_user=$(getent passwd "${USER_UID}" | cut -d: -f1 || true) \
    && if [ -n "$existing_user" ] && [ "$existing_user" != "${USERNAME}" ]; then \
         usermod -l "${USERNAME}" -d /home/${USERNAME} -m "$existing_user"; \
         groupmod -n "${USERNAME}" "$(id -gn "${USERNAME}" 2>/dev/null || echo "$existing_user")"; \
       elif [ -z "$existing_user" ]; then \
         groupadd --force --gid "${USER_GID}" "${USERNAME}"; \
         useradd --uid "${USER_UID}" --gid "${USER_GID}" --create-home --shell /bin/bash "${USERNAME}"; \
       fi \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
    && chmod 0440 "/etc/sudoers.d/${USERNAME}" \
    && echo '[ -n "$VIRTUAL_ENV" ] && . "$VIRTUAL_ENV/bin/activate"' >> /home/${USERNAME}/.bashrc
