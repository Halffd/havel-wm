# Maintainer: Havel WM Team <havel@example.com>
# Contributor: Your Name <your.email@example.com>

pkgname=havel-wm
pkgver=0.1.0
pkgrel=1
pkgdesc="A modern Wayland compositor built on wlroots with plugins and IPC"
arch=('x86_64')
url="https://github.com/havel-wm/havel-wm"
license=('MIT')
depends=(
    'wlroots0.20'
    'wayland'
    'libxkbcommon'
    'pixman'
    'dbus'
    'glib2'
    'nlohmann-json'
    'libpng'
    'freetype2'
    'vulkan-icd-loader'
    'vulkan-tools'
    'spirv-tools'
)
makedepends=(
    'cmake'
    'ninja'
    'git'
    'wayland-protocols'
)
optdepends=(
    'waybar: Panel/bar support'
    'wofi: Application launcher'
    'mako: Notification daemon'
    'foot: Terminal emulator'
    'grim: Screenshot utility'
    'slurp: Region selection'
    'wlr-randr: Output configuration'
    'gammastep: Gamma/brightness control'
)
provides=('wayland-compositor')
conflicts=('sway' 'weston' 'river')
backup=()
options=('!strip')

# Git version for development builds
# pkgver() {
#     cd "$pkgname"
#     git describe --long --tags --always --dirty | sed 's/\([^-]*-g\)/r\1/;s/-/./g'
# }

source=(
    "$pkgname-$pkgver.tar.gz::https://github.com/havel-wm/havel-wm/archive/refs/tags/v$pkgver.tar.gz"
)
sha256sums=('SKIP')  # Replace with actual checksum

prepare() {
    cd "$pkgname-$pkgver"
    
    # Apply patches if needed
    # for patch in "${source[@]}"; do
    #     case $patch in
    #         *.patch) patch -Np1 < "$srcdir/$patch" ;;
    #     esac
    # done
}

build() {
    cd "$pkgname-$pkgver"
    
    # Create build directory
    cmake -B build \
        -S . \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DWLR_USE_UNSTABLE=ON
    
    # Build
    cmake --build build
}

check() {
    cd "$pkgname-$pkgver"
    
    # Run tests if available
    # cmake --build build --target test
    echo "No tests available yet"
}

package() {
    cd "$pkgname-$pkgver"
    
    # Install
    DESTDIR="$pkgdir" cmake --install build
    
    # Install systemd user service
    install -Dm644 systemd/havel-wm.service \
        "$pkgdir/usr/lib/systemd/user/havel-wm.service"
    
    # Install desktop file
    install -Dm644 havel-wm.desktop \
        "$pkgdir/usr/share/wayland-sessions/havel-wm.desktop"
    
    # Install license
    install -Dm644 LICENSE \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    
    # Install documentation
    install -Dm644 README.md \
        "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 PROTOCOLS.md \
        "$pkgdir/usr/share/doc/$pkgname/PROTOCOLS.md"
    install -Dm644 STATUS.md \
        "$pkgdir/usr/share/doc/$pkgname/STATUS.md"
    
    # Install example config
    install -Dm644 plugins.json.example \
        "$pkgdir/usr/share/doc/$pkgname/plugins.json.example"
    
    # Install IPC test script
    install -Dm755 test_ipc.sh \
        "$pkgdir/usr/share/doc/$pkgname/test_ipc.sh"
}

# vim:set ts=8 sw=4 et:
