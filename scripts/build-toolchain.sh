#!/bin/bash
# Build the sh4eb-linux-muslfdpic cross toolchain (big-endian SH-4A, no
# FPU, FDPIC, musl) into $TOOLCHAIN.
#
# The same compiler builds the kernel (arch/sh adds -mno-fdpic), the
# loader add-in (-mno-fdpic -ffreestanding) and the FDPIC user space.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

PREFIX=$TOOLCHAIN
SYSROOT=$PREFIX/$TARGET
B=$WORK/toolchain-build
export PATH=$PREFIX/bin:$PATH
mkdir -p "$B" "$PREFIX"

fetch "$BINUTILS_URL" "$BINUTILS_SHA256"
fetch "$GCC_URL" "$GCC_SHA256"
fetch "$MUSL_URL" "$MUSL_SHA256"
cd "$B"
for t in "$BINUTILS_URL" "$GCC_URL" "$MUSL_URL"; do
	d=$(basename "$t" | sed -E 's/\.tar\.(gz|xz|bz2)$//')
	[ -d "$d" ] || tar xf "$DL/$(basename "$t")"
done

# --- binutils -----------------------------------------------------------
if [ ! -x "$PREFIX/bin/$TARGET-ld" ]; then
	rm -rf b-binutils && mkdir b-binutils && cd b-binutils
	../binutils-$BINUTILS_VERSION/configure --target=$TARGET --prefix="$PREFIX" \
		--with-sysroot="$SYSROOT" --disable-nls --disable-werror \
		--disable-gdb --disable-gprofng --disable-sim
	make -j"$JOBS"
	make install
	cd ..
fi

# --- headers: musl and the kernel's UAPI --------------------------------
if [ ! -f "$SYSROOT/usr/include/stdio.h" ]; then
	make -C musl-$MUSL_VERSION ARCH=sh prefix=/usr DESTDIR="$SYSROOT" install-headers
fi
if [ ! -f "$SYSROOT/usr/include/linux/version.h" ]; then
	KSRC=$(source_tree linux "$KERNEL_REPO" "$KERNEL_REF" "${KERNEL_SRC:-}")
	make -C "$KSRC" ARCH=sh O="$B/linux-headers" INSTALL_HDR_PATH="$SYSROOT/usr" headers_install
fi

# --- gcc ----------------------------------------------------------------
GCC_CONF=(--target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT"
	--with-build-sysroot="$SYSROOT"
	--with-cpu=m4a-nofpu --with-endian=big --enable-fdpic
	--disable-multilib --with-multilib-list=
	--enable-languages=c --disable-shared --disable-nls --disable-bootstrap
	--disable-libssp --disable-libquadmath --disable-libgomp
	--disable-libatomic --disable-libitm --disable-libsanitizer
	--disable-libvtv --disable-libstdcxx --disable-decimal-float
	--enable-tls --enable-threads=posix --with-pkgversion="fxcg20-linux")
if [ ! -x "$PREFIX/bin/$TARGET-gcc" ]; then
	if [ ! -f b-gcc/Makefile ]; then
		rm -rf b-gcc && mkdir b-gcc
		(cd b-gcc && ../gcc-$GCC_VERSION/configure "${GCC_CONF[@]}")
	fi
	make -C b-gcc -j"$JOBS" all-gcc
	make -C b-gcc install-gcc
fi
if [ ! -f "$("$PREFIX/bin/$TARGET-gcc" -print-libgcc-file-name)" ]; then
	make -C b-gcc -j"$JOBS" all-target-libgcc
	make -C b-gcc install-target-libgcc
fi

# --- musl ---------------------------------------------------------------
if [ ! -f "$SYSROOT/usr/lib/libc.a" ]; then
	rm -rf b-musl && mkdir b-musl && cd b-musl
	# oldmalloc: smaller, and without an MMU the guard pages of mallocng
	# take real memory
	../musl-$MUSL_VERSION/configure --target=$TARGET --prefix=/usr \
		--disable-shared --with-malloc=oldmalloc \
		CC=$TARGET-gcc CROSS_COMPILE=$TARGET-
	make -j"$JOBS"
	make DESTDIR="$SYSROOT" install
	cd ..
fi

# --- SH FDPIC fix-up ----------------------------------------------------
# GCC's crtstuff registers frame_dummy/__do_global_dtors_aux in .init_array
# and .fini_array with a plain R_SH_DIR32 code address instead of an FDPIC
# function descriptor, so every program crashes in __libc_start_init. musl
# runs .init_array itself and C programs need nothing from crtstuff:
# replace the objects with empty ones.
GCCLIB=$(dirname "$("$PREFIX/bin/$TARGET-gcc" -print-libgcc-file-name)")
if [ ! -f "$GCCLIB/.crtstuff-emptied" ]; then
	for o in crtbegin.o crtbeginS.o crtbeginT.o crtend.o crtendS.o; do
		echo "" | "$PREFIX/bin/$TARGET-gcc" -x c -c -o "$GCCLIB/$o" -
	done
	touch "$GCCLIB/.crtstuff-emptied"
fi

"$PREFIX/bin/$TARGET-gcc" --version | head -1
