# project_n-wine: project_n-linux plus Wine, the open-source half of the msvc image. Kept as its
# own image so CI can cache it in the registry; the MSVC half can't be (licence).
# project_n.py passes the content-hashed project_n-linux tag.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ARG APT_SNAPSHOT=20261029T000000Z

USER root
RUN apt-get update --snapshot "$APT_SNAPSHOT" \
 && apt-get install -y --no-install-recommends wine wine64 winbind msitools \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir -p /opt/msvc-wine /opt/msvc && chown dev:dev /opt/msvc-wine /opt/msvc
USER dev
# Ubuntu keeps wineserver off PATH.
ENV PATH=$PATH:/usr/lib/x86_64-linux-gnu/wine WINEDEBUG=-all
