# syntax=docker/dockerfile:1

# ---------------------------------------------------------------------------
# Build stage — Alpine/musl. esphome-api-client v0.3.0 carries the wire layer
# and the Noise crypto in-tree, so the only thing fetched for the library is
# header-only Asio — there are no external native prerequisites to build or
# install. We link fully static (musl libc + libgcc + libstdc++) so the
# resulting binary has no shared dependencies and runs on the empty
# distroless/static base. The whole builder is discarded.
# ---------------------------------------------------------------------------
FROM alpine:3.21 AS build

RUN apk add --no-cache \
        build-base \
        cmake \
        ninja \
        git \
        linux-headers \
        ca-certificates

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DANTENNA_SWITCHER_BUILD_CLI=ON \
        -DANTENNA_SWITCHER_BUILD_TESTS=OFF \
        -DANTENNA_SWITCHER_BUILD_EXAMPLES=OFF \
        -DANTENNA_SWITCHER_INSTALL=OFF \
        -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
    && cmake --build build --target antenna-switcher-cli -j "$(nproc)" \
    && strip build/bin/antenna-switcher-cli

# ---------------------------------------------------------------------------
# Runtime stage — distroless/static (no libc, no shell). The fully-static
# binary needs nothing from the base image.
# ---------------------------------------------------------------------------
FROM gcr.io/distroless/static-debian12 AS runtime

COPY --from=build /src/build/bin/antenna-switcher-cli /usr/local/bin/antenna-switcher-cli

ENTRYPOINT ["/usr/local/bin/antenna-switcher-cli"]
