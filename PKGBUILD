pkgname=brainfuck-vm
pkgver=0.0.0
pkgrel=1
pkgdesc="Brainfuck VM"
arch=('x86_64')
url="https://github.com/maksverver/BrainfuckVM"
depends=()

build() {
    cd "${startdir}"

    make ${MAKEFLAGS} CPPFLAGS="${CPPFLAGS}" CFLAGS="${CFLAGS}" CXXFLAGS="${CXXFLAGS}" LDFLAGS="${LDFLAGS}"
}

check() {
    cd "${startdir}"

    make test
}

package() {
    cd "${startdir}"

    install -d "${pkgdir}/usr/bin/"
    install bfi "${pkgdir}/usr/bin/"
}
