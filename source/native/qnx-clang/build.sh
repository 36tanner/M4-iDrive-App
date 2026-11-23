#!/bin/sh
# Build ID6 workers for QNX Neutrino 6.5 (armle-v7) with stock clang and lld, without the QNX SDP.
#
#   sh native/qnx-clang/build.sh            # from source/; writes native/qnx-clang/out/
#
# Needs clang, ld.lld, llvm-ar and git (for LLVM's compiler-rt builtins, Apache-2.0 with LLVM exception).
# Nothing from QNX is used: the headers in include/ are written for this project, the start-up code is
# crt0.S, and the libraries linked against are empty stand-ins that only carry QNX's library names. The
# head unit's own libc.so.3, libm.so.2 and libsocket.so.3 are used when the program runs.
#
# Workers only. The C++ plugins need BMW's frameworks and GCC 4.4's C++ ABI and are not built here.
set -eu
cd "$(dirname "$0")"
here=$PWD
native=$here/..
work=$here/work
out=$here/out
mkdir -p "$work/rt" "$work/obj" "$work/stubs" "$out"
res=$(clang -print-resource-dir)
target="--target=armv7a-unknown-none-eabi -marm -mfloat-abi=soft -mcpu=cortex-a9"
cflags="$target -O2 -std=c99 -ffreestanding -nostdinc -isystem $res/include -I$here/include -fPIC \
 -fno-unwind-tables -fno-asynchronous-unwind-tables -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ \
 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter"

# 1. Software floating point and division helpers, as the stock GCC build links from libgcc.
if test ! -f "$work/librt.a"; then
    if test ! -d "$work/llvm/compiler-rt/lib/builtins"; then
        git clone -q --depth 1 --filter=blob:none --sparse --branch llvmorg-18.1.3 \
            https://github.com/llvm/llvm-project.git "$work/llvm"
        (cd "$work/llvm" && git sparse-checkout set compiler-rt/lib/builtins)
    fi
    b=$work/llvm/compiler-rt/lib/builtins
    rtflags="$target -mfpu=none -O2 -ffreestanding -fPIC -fno-unwind-tables -w -I$b"
    for f in "$b"/*.c; do
        n=$(basename "$f" .c)
        case $n in *vfp*|emutls|enable_execute_stack|clear_cache|cpu_model|os_version_check|gcc_personality_v0|\
            eprintf|atomic*|trampoline_setup|apple_versioning|*tf*|*xf*|*tc*|*ti*|*128*|*bf*|*hf*) continue;; esac
        clang $rtflags -c "$f" -o "$work/rt/$n.o" 2>/dev/null || true
    done
    for f in "$b"/arm/*.S "$b"/arm/*.c; do
        n=arm_$(basename "${f%.*}")
        case $n in *vfp*|*sync*|arm_chkstk|arm_fp_mode|arm_switch*) continue;; esac
        clang $rtflags -c "$f" -o "$work/rt/$n.o" 2>/dev/null || true
    done
    llvm-ar rcs "$work/librt.a" "$work"/rt/*.o
fi

# 2. Stand-in libraries. Data objects keep QNX's sizes: errno 4, _Ctype 4, FILE 80.
libc_funcs="_init_libc _preinit_array _init_array _fini_array atexit exit __get_errno_ptr access alarm \
 clock_gettime close dlclose dlerror dlopen dlsym fcntl fprintf fputc fputs getppid inet_ntop inet_pton memchr \
 memcmp memcpy memset perror poll pthread_cond_broadcast pthread_cond_init pthread_cond_timedwait \
 pthread_condattr_destroy pthread_condattr_init pthread_condattr_setclock pthread_create pthread_join \
 pthread_mutex_init pthread_mutex_lock pthread_mutex_unlock pthread_sigmask sigaction sigaddset sigemptyset \
 signal sigprocmask snprintf strcmp strcpy strlen strtod strtoul strtoull vfprintf fflush fopen fclose fwrite \
 printf puts time ctime mkdir unlink open strncmp strchr malloc free"
stub() {
    name=$1; soname=$2; shift 2
    { for s in "$@"; do echo "void $s(void) {}"; done; } > "$work/stubs/$name.c"
    test "$name" = libc && echo 'int errno; const short *_Ctype; struct { char b[80]; } _Stdin, _Stdout, _Stderr;' \
        >> "$work/stubs/$name.c"
    clang $target -fPIC -ffreestanding -fno-builtin -w -c "$work/stubs/$name.c" -o "$work/stubs/$name.o"
    ld.lld -shared -soname "$soname" --hash-style=sysv "$work/stubs/$name.o" -o "$work/stubs/$name.so"
}
# shellcheck disable=SC2086
stub libc libc.so.3 $libc_funcs
stub libm libm.so.2 round _Sin floor sqrt
stub libsocket libsocket.so.3 socket bind connect send recv sendto recvfrom setsockopt getsockopt
clang $cflags -c "$here/crt0.S" -o "$work/crt0.o"

# 3. One worker: name, extra defines, sources.
link() {
    exe=$1; shift
    ld.lld -m armelf --dynamic-linker=/usr/lib/ldqnx.so.2 --image-base=0x100000 --hash-style=sysv \
        --no-rosegment -z norelro -z nognustack -z max-page-size=4096 --build-id=none -e _start \
        "$work/crt0.o" "$@" "$work/librt.a" -L "$work/stubs" -lsocket -lm -lc -o "$out/$exe"
    # EABI version 5 with the entry flag, exactly as the stock workers.
    python3 -c "import struct,sys;p=sys.argv[1];b=bytearray(open(p,'rb').read());\
struct.pack_into('<I',b,0x24,0x05000002);open(p,'wb').write(b)" "$out/$exe"
}
worker() {
    exe=$1; defines=$2; shift 2
    objs=""
    for f in "$@" "$native"/src/common/gauge_core.c "$native"/src/common/diag_link.c "$native"/src/common/most/*.c; do
        o=$work/obj/$exe-$(basename "$f" .c).o
        # shellcheck disable=SC2086
        clang $cflags $defines -I"$native/src/common" -I"$native/src/ipc" -I"$native/src/workers" -c "$f" -o "$o"
        objs="$objs $o"
    done
    # shellcheck disable=SC2086
    link "$exe" $objs
}
worker boostdeck-collector-qnx "" "$native/src/workers/boost_deck_worker.c" "$here/boost_tone_stub.c"
worker boostdeck-collector-s55 "-DS55_ABSOLUTE_OIL" "$native/src/workers/boost_deck_worker.c" "$here/boost_tone_stub.c"
(cd "$out" && sha256sum boostdeck-collector-qnx boostdeck-collector-s55 > SHA256SUMS)
echo "Built: $out"
