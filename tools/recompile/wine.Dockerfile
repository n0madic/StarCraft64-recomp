# Local Linux x86_64 image with Wine, used only to run the cross-built Windows
# N64Recomp.exe on a non-Windows host and compare its output with the recipe
# hashes (ADR-085). Same pinned Debian as the other images; built locally on
# first use, no registry push and no remote build.
FROM debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update \
 && apt-get install -y --no-install-recommends wine64 \
 && rm -rf /var/lib/apt/lists/*
