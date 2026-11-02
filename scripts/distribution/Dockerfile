FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 dpkg-dev rpm createrepo-c pacman-package-manager makepkg \
    libarchive-tools zstd gnupg curl && rm -rf /var/lib/apt/lists/*
WORKDIR /work
ENTRYPOINT ["python3", "scripts/distribution/linux.py"]
