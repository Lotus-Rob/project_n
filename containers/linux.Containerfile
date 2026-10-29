# project_n-linux: Linux artefact builds and container checks.
# Toolchains come from pixi.lock (the same as native development), never from the distro.
FROM docker.io/library/ubuntu@sha256:157dc3d6e4a3e4f2c68d12df9df9ed6e808b59af6d4c3757b432b4f519916725

ARG PIXI_VERSION=0.83.0
ARG PIXI_SHA256=064f371e692aaa3400ac581c89da2843c91437f20c2d9155395a5d0037f33551
ARG APT_SNAPSHOT=20261029T000000Z

# Only what Pixi and XMake need to download and unpack sources. Ubuntu 25.10+ defaults to
# uutils coreutils, whose `ln -sf`/`install` break libtool installs ("Already exists"): use GNU.
RUN apt-get update --snapshot "$APT_SNAPSHOT" \
 && apt-get install -y --no-install-recommends --allow-remove-essential coreutils-from-gnu coreutils-from-uutils- \
 && apt-get install -y --no-install-recommends ca-certificates curl git unzip xz-utils bzip2 patch \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/pixi.tar.gz "https://github.com/prefix-dev/pixi/releases/download/v${PIXI_VERSION}/pixi-x86_64-unknown-linux-musl.tar.gz" \
 && echo "${PIXI_SHA256}  /tmp/pixi.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/pixi.tar.gz -C /usr/local/bin pixi \
 && rm /tmp/pixi.tar.gz

# Ubuntu's default uid 1000 user, renamed; runs with --userns=keep-id so outputs belong to the host user.
RUN usermod -l dev -d /home/dev -m ubuntu && groupmod -n dev ubuntu \
 && mkdir -p /src /home/dev/.xmake && chown dev:dev /src /home/dev/.xmake
USER dev
ENV PROJECT_N_IN_CONTAINER=1

# The repo is bind-mounted at /src at run time; keep the env outside it so the host's .pixi/ is untouched.
RUN pixi config set --global detached-environments /home/dev/.pixi-envs
WORKDIR /src
COPY --chown=dev:dev pixi.toml pixi.lock /src/
COPY --chown=dev:dev tools/env/activate.sh /src/tools/env/
# Drop Pixi's download cache: the installed environment is all the image needs.
RUN pixi install --locked && rm -rf /home/dev/.cache/rattler
