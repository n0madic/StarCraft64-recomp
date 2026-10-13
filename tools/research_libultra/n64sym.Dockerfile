# Local Linux image that builds shygoo/n64sym (third_party/n64sym) when no binary
# is available for the host: `make` with a static g++ link. The base is the same
# pinned Debian as tools/desktop/linux-cross.Dockerfile; tools/dockerenv.py builds
# it locally on first use (no registry push, no remote build).
FROM debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make \
 && rm -rf /var/lib/apt/lists/*
