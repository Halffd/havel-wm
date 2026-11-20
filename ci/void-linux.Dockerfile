FROM ghcr.io/void-linux/void-musl-full:latest

ARG NLOHMANN_JSON_VERSION=3.11.3
ARG NLOHMANN_JSON_SHA256=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6

# Void does not package nlohmann/json, so install the pinned single header.
RUN xbps-install -Suy \
        bash \
        base-devel \
        cmake \
        ninja \
        pkg-config \
        wayland-devel \
        wlroots0.20-devel \
        libxkbcommon-devel \
        pixman-devel \
        fmt-devel \
        freetype-devel \
        pam-devel \
        libpng-devel \
        librsvg-devel \
        libcurl-devel \
        dbus-devel \
        MesaLib-devel \
        libgbm-devel \
        libglvnd-devel \
        vulkan-loader \
        python3 \
        python3-Pillow \
        grim \
        foot \
        xdg-utils \
        curl && \
    curl -fsSL --retry 3 \
        "https://raw.githubusercontent.com/nlohmann/json/v${NLOHMANN_JSON_VERSION}/single_include/nlohmann/json.hpp" \
        -o /tmp/nlohmann-json.hpp && \
    echo "${NLOHMANN_JSON_SHA256}  /tmp/nlohmann-json.hpp" | sha256sum -c - && \
    install -Dm644 /tmp/nlohmann-json.hpp \
        /usr/local/include/nlohmann/json.hpp && \
    rm -f /tmp/nlohmann-json.hpp

ENV LIBGL_ALWAYS_SOFTWARE=1

WORKDIR /src
