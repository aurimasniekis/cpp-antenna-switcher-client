include_guard(GLOBAL)

# CPack configuration for the `antenna-switcher-cli` command-line tool, used to
# produce distro packages (.deb / .rpm) that are attached to GitHub releases
# alongside the raw binaries (see .github/workflows/release-packages.yml).
#
# The release workflow configures with the library install OFF and the CLI
# install ON, so the only staged file is the CLI binary and the resulting
# package ships just `/usr/bin/antenna-switcher-cli` — no library archives or
# headers:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#         -DANTENNA_SWITCHER_BUILD_CLI=ON -DANTENNA_SWITCHER_BUILD_TESTS=OFF \
#         -DANTENNA_SWITCHER_BUILD_EXAMPLES=OFF \
#         -DANTENNA_SWITCHER_INSTALL=OFF -DANTENNA_SWITCHER_INSTALL_CLI=ON
#   cmake --build build --target antenna-switcher-cli
#   cpack --config build/CPackConfig.cmake -G DEB -B dist
#   cpack --config build/CPackConfig.cmake -G RPM -B dist

set(CPACK_PACKAGE_NAME    "antenna-switcher-cli")
set(CPACK_PACKAGE_VENDOR  "Aurimas Niekis")
set(CPACK_PACKAGE_CONTACT "Aurimas Niekis <aurimas@niekis.lt>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Command-line client for the antenna-switcher device over the ESPHome native API")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/aurimasniekis/cpp-antenna-switcher-client")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Package only the CLI binary; strip it. The library/header install components
# (when ANTENNA_SWITCHER_INSTALL is on) are deliberately left out of the packages.
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "antenna-switcher-cli")

# Install under /usr (so the binary lands in /usr/bin, not /usr/local/bin). This
# is the DEB/RPM generator default, set explicitly for determinism.
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Distro-native file names: <name>_<version>_<arch>.{deb,rpm}.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RPM_FILE_NAME    RPM-DEFAULT)

# --- Debian ----------------------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION    "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE   "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Compute the runtime library Depends (libc6, libstdc++6, ...) from the binary
# via dpkg-shlibdeps so the package declares its actual minimum versions.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# --- RPM -------------------------------------------------------------------
set(CPACK_RPM_PACKAGE_LICENSE     "MIT")
set(CPACK_RPM_PACKAGE_GROUP       "Applications/System")
set(CPACK_RPM_PACKAGE_URL         "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")

include(CPack)
