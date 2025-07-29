# Stage 1: Build stage
FROM archlinux:latest AS builder

WORKDIR /

# Install Meson and Ninja
RUN pacman -Syu --noconfirm
RUN pacman -S --noconfirm meson ninja gcc cmake git poppler opencv
RUN rm -rf /var/cache/pacman/pkg/*

COPY . .
RUN meson setup ./build/
RUN ninja -C ./build/

# Stage 2: Runtime stage
FROM archlinux:latest as runtime-image

WORKDIR /app

COPY --from=builder /build/bin/ptv .

CMD ["./ptv"]
