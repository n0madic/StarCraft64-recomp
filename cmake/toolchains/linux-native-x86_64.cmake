# Build the Linux x86_64 shell with the host's own compiler on a Linux x86_64 machine
# (no Docker image). Same static libstdc++/libgcc policy as linux-x86_64.cmake;
# glibc and the windowing/audio libraries SDL opens at run time come from the
# machine that runs the package, so a build host with a newer glibc than the
# target distribution produces a binary that distribution cannot start.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
