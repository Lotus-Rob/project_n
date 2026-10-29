# project_n-msvc: windows-msvc artefacts built on Linux with the real MSVC under Wine (msvc-wine).
# Local-only: the MSVC licence forbids redistribution, so this image is never pushed.
# project_n.py passes the content-hashed project_n-linux tag.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ARG APT_SNAPSHOT=20261029T000000Z
ARG MSVC_WINE_COMMIT=514f8ea34842cd6d831804d0e9658d3a32870ae1
ARG MSVC_VERSION=18.0
ARG SDK_VERSION=10.0.26100
ARG ACCEPT_MSVC_LICENSE=0

USER root
RUN [ "$ACCEPT_MSVC_LICENSE" = 1 ] || { echo "This image downloads MSVC: pass --build-arg ACCEPT_MSVC_LICENSE=1 to accept its licence."; exit 1; }
RUN apt-get update --snapshot "$APT_SNAPSHOT" \
 && apt-get install -y --no-install-recommends wine wine64 winbind msitools \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir -p /opt/msvc-wine /opt/msvc && chown dev:dev /opt/msvc-wine /opt/msvc
USER dev

RUN git clone -q https://github.com/mstorsjo/msvc-wine /opt/msvc-wine \
 && git -C /opt/msvc-wine checkout -q "$MSVC_WINE_COMMIT"
# vsdownload.py runs with the Pixi env's Python, so no distro Python is needed.
RUN pixi run python /opt/msvc-wine/vsdownload.py --accept-license \
        --msvc-version "$MSVC_VERSION" --sdk-version "$SDK_VERSION" \
        --architecture x64 --dest /opt/msvc \
 && /opt/msvc-wine/install.sh /opt/msvc \
 && rm -rf /tmp/*
# Ubuntu keeps wineserver off PATH. Recreate the prefix explicitly and wait for wineserver to
# flush it: a prefix left by a RUN step that exits before the flush lacks HKCU\Environment
# (no %TEMP%), and cl.exe then fails with D8037 "cannot create temporary il file".
ENV PATH=$PATH:/usr/lib/x86_64-linux-gnu/wine WINEDEBUG=-all
RUN rm -rf /home/dev/.wine && wine64 wineboot -i && wineserver -w
