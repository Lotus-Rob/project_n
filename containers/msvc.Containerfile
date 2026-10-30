# project_n-msvc: windows-msvc artefacts built on Linux with the real MSVC under Wine (msvc-wine).
# Holds no Microsoft files, so CI caches it in the registry: `project_n image` installs MSVC and
# the SDK once into ~/.xmake/project_n-tools/msvc-<hash of this file>, mounted at /opt/msvc
# (the MSVC licence forbids redistribution). project_n.py passes the content-hashed linux tag.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ARG APT_SNAPSHOT=20261029T000000Z
ARG MSVC_WINE_COMMIT=514f8ea34842cd6d831804d0e9658d3a32870ae1

USER root
RUN apt-get update --snapshot "$APT_SNAPSHOT" \
 && apt-get install -y --no-install-recommends wine wine64 winbind msitools \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir -p /opt/msvc-wine /opt/msvc && chown dev:dev /opt/msvc-wine /opt/msvc
USER dev

RUN git clone -q https://github.com/mstorsjo/msvc-wine /opt/msvc-wine \
 && git -C /opt/msvc-wine checkout -q "$MSVC_WINE_COMMIT"
# What `project_n image` installs into /opt/msvc. Changing either changes this file's hash,
# and with it the install directory.
ENV MSVC_VERSION=18.0 SDK_VERSION=10.0.26100
# Ubuntu keeps wineserver off PATH. Recreate the prefix explicitly and wait for wineserver to
# flush it: a prefix left by a RUN step that exits before the flush lacks HKCU\Environment
# (no %TEMP%), and cl.exe then fails with D8037 "cannot create temporary il file".
ENV PATH=$PATH:/usr/lib/x86_64-linux-gnu/wine WINEDEBUG=-all
RUN rm -rf /home/dev/.wine && wine64 wineboot -i && wineserver -w
