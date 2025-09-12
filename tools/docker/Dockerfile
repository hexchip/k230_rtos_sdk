# Use an official Ubuntu as a base image
FROM ubuntu:22.04 AS k230_rtos_sdk

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG UID=1000
ARG USER=hexchip

# Set the environment to non-interactive to avoid interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# config ubuntu local apt source
RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list \
    && sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list

RUN rm -f /etc/apt/apt.conf.d/docker-clean; echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' > /etc/apt/apt.conf.d/keep-cache

# Update and install system dependencies
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    apt-get install -y --no-install-recommends \
    make \
    autoconf \
    automake \
    bison \
    flex \
    gcc \
    g++ \
    gawk \
    libncurses5-dev \
    pkg-config \
    libconfuse-dev \
    libssl-dev \
    python3 \
    python3-pip \
    python-is-python3 \
    cmake \
    libyaml-dev \
    scons \
    mtools \
    bzip2 \
    curl \
    git \
    repo \
    openssh-client \
    rsync \
    dosfstools \
    sudo \
    ca-certificates \
    && update-ca-certificates

RUN pip3 config --global set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple

# Install required Python packages
RUN pip3 install --no-cache-dir pycryptodome gmssl scons==3.1.2

ENV REPO_URL='https://mirrors.tuna.tsinghua.edu.cn/git/git-repo'

# Add user
RUN groupadd --gid ${UID} ${USER} \
    && useradd --uid ${UID} --gid ${USER} --shell /bin/bash --create-home ${USER} \
    && usermod -aG sudo ${USER} \
    && echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER ${USER}

# Create base directories for the toolchains
RUN mkdir -p ~/.kendryte/k230_toolchains/

# Download and extract the RISC-V toolchain
RUN curl -L https://kendryte-download.canaan-creative.com/k230/toolchain/riscv64-unknown-linux-musl-rv64imafdcv-lp64d-20230420.tar.bz2 -o ~/.kendryte/k230_toolchains/riscv64-unknown-linux-musl-rv64imafdcv-lp64d-20230420.tar.bz2 && \
    tar xf ~/.kendryte/k230_toolchains/riscv64-unknown-linux-musl-rv64imafdcv-lp64d-20230420.tar.bz2 -C ~/.kendryte/k230_toolchains/ && \
    rm -rf ~/.kendryte/k230_toolchains/riscv64-unknown-linux-musl-rv64imafdcv-lp64d-20230420.tar.bz2

# Download and extract the Xuantie-900 toolchain
RUN curl -L https://kendryte-download.canaan-creative.com/k230/toolchain/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0.tar.bz2 -o ~/.kendryte/k230_toolchains/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0.tar.bz2 && \
    tar xf ~/.kendryte/k230_toolchains/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0.tar.bz2 -C ~/.kendryte/k230_toolchains/ && \
    rm -rf ~/.kendryte/k230_toolchains/Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0.tar.bz2

# Set the working directory for the project
WORKDIR /home/${USER}/


FROM k230_rtos_sdk AS devcontainer

ARG TZ="Asia/Shanghai"
ENV TZ="$TZ"

ARG CLAUDE_CODE_VERSION=latest

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        less \
        procps \
        fzf \
        zsh \
        man-db \
        unzip \
        gnupg2 \
        jq \
        vim \
        tree \
        wget \
        pulseaudio

ENV PNPM_HOME="/pnpm"
ENV PATH="$PNPM_HOME:$PATH"
# install pnpm
RUN wget -qO- https://get.pnpm.io/install.sh | ENV="$HOME/.bashrc" SHELL="$(which bash)" bash -

# install nodejs
RUN pnpm env use --global lts \
  && pnpm config -g set registry https://registry.npmmirror.com

# Install uv
COPY --from=ghcr.io/astral-sh/uv:latest /uv /uvx /bin/

# install git delta
ARG GIT_DELTA_VERSION=0.18.2
RUN ARCH=$(dpkg --print-architecture) && \
  wget "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  sudo dpkg -i "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb"

# Persist bash history.
RUN mkdir /commandhistory \
  && touch /commandhistory/.bash_history \
  && chown -R ${USER} /commandhistory

# Set `DEVCONTAINER` environment variable to help with orientation
ENV DEVCONTAINER=true

# Set the default shell to zsh rather than sh
ENV SHELL=/bin/zsh

# Set the default editor and visual
ENV EDITOR=vim
ENV VISUAL=vim

# Install Claude
RUN pnpm add -g @anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}

USER ${USER}

# Create config directories and set permissions
RUN mkdir .claude

# Default powerline10k theme
ARG ZSH_IN_DOCKER_VERSION=1.2.0
RUN sh -c "$(wget -O- https://github.com/deluan/zsh-in-docker/releases/download/v${ZSH_IN_DOCKER_VERSION}/zsh-in-docker.sh)" -- \
  -p git \
  -a "export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  -x

