# Sources and versions used by the build scripts. Every value can be
# overridden from the environment.

# Kernel with the fx-CG20 support (Linux 7.2.y stable + SH7305/fx-CG patches)
: "${KERNEL_REPO:=https://github.com/MaxSignal/linux.git}"
: "${KERNEL_REF:=fxcg20-7.2.y}"

# Linux loader (a Casio OS add-in)
: "${LOADER_REPO:=https://github.com/MaxSignal/fxcg-linux-loader.git}"
: "${LOADER_REF:=master}"

# Emulator used for the boot test (casio-cg50 with a bare-metal fx-CG20 mode)
: "${EMU_REPO:=https://github.com/MaxSignal/casio-cg50.git}"
: "${EMU_REF:=linux-direct-boot}"

# Toolchain: sh4eb-linux-muslfdpic (big-endian SH-4A, no FPU, FDPIC, musl)
: "${BINUTILS_VERSION:=2.42}"
: "${BINUTILS_SHA256:=f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800}"
: "${BINUTILS_URL:=https://ftpmirror.gnu.org/binutils/binutils-${BINUTILS_VERSION}.tar.xz}"
: "${GCC_VERSION:=14.2.0}"
: "${GCC_SHA256:=a7b39bc69cbf9e25826c5a60ab26477001f7c08d85cec04bc0e29cabed6f3cc9}"
: "${GCC_URL:=https://ftpmirror.gnu.org/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz}"
: "${MUSL_VERSION:=1.2.4}"
: "${MUSL_SHA256:=7a35eae33d5372a7c0da1188de798726f68825513b7ae3ebe97aaaa52114f039}"
: "${MUSL_URL:=https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz}"

# Userspace
: "${BUSYBOX_VERSION:=1.36.1}"
: "${BUSYBOX_SHA256:=b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314}"
: "${BUSYBOX_URL:=https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2}"

# DEBUG=1: bring-up build for real calculators. The kernel draws coloured
# squares at the bottom right of the screen as it boots (yellow:
# decompressing, green: decompressed, white: kernel board setup; red:
# decompression failed) until the console takes over. The package is
# named *-debug. Not for releases.
: "${DEBUG:=0}"

: "${OUT:=$(pwd)/out}"
: "${WORK:=$(pwd)/work}"
: "${DL:=$WORK/dl}"
: "${TOOLCHAIN:=$WORK/toolchain}"
TARGET=sh4eb-linux-muslfdpic
CROSS_COMPILE=$TOOLCHAIN/bin/$TARGET-
JOBS=${JOBS:-$(nproc)}
mkdir -p "$OUT" "$WORK" "$DL"

# fetch URL SHA256: download into $DL unless already there, check it.
fetch() {
	f=$DL/$(basename "$1")
	if [ ! -f "$f" ]; then
		echo ">> downloading $1"
		curl -fL --retry 3 -o "$f.part" "$1" && mv "$f.part" "$f"
	fi
	echo "$2  $f" | sha256sum -c --quiet - || { echo "checksum mismatch: $f" >&2; exit 1; }
}

# source_tree NAME REPO REF [LOCAL]: echo a checkout of REPO at REF in
# $WORK/NAME (cloned once), or LOCAL when it is given.
source_tree() {
	if [ -n "${4:-}" ]; then
		echo "$4"
		return
	fi
	d=$WORK/$1
	# reuse the checkout only while it is still what REF points to
	want=$(git ls-remote "$2" "$3" "refs/tags/$3^{}" | tail -1 | cut -f1)
	stale=0
	[ -d "$d/.git" ] || stale=1
	[ "$(cat "$d/.git/fxcg-ref" 2>/dev/null)" = "$2 $3" ] || stale=1
	if [ $stale = 0 ] && [ -n "$want" ] && [ "$(git -C "$d" rev-parse HEAD)" != "$want" ]; then
		stale=1
	fi
	if [ $stale = 1 ]; then
		rm -rf "$d"
		git clone -q --depth 1 --branch "$3" "$2" "$d" >&2
		echo "$2 $3" > "$d/.git/fxcg-ref"
	fi
	echo "$d"
}
