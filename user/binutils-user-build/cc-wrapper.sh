#!/usr/bin/env sh
set -e

user_objs="/home/seamu/Coding/1_Repos/Personal/Serotonin/user/../sysroot/usr/lib/crt0.o"
user_ldflags="-nostartfiles -T /home/seamu/Coding/1_Repos/Personal/Serotonin/user/../sysroot/usr/lib/user.ld --sysroot=/home/seamu/Coding/1_Repos/Personal/Serotonin/user/../sysroot -L/home/seamu/Coding/1_Repos/Personal/Serotonin/user/../sysroot/usr/lib"
user_libs="-Wl,--start-group -lsyscall -lcxxrt -lc -lm -Wl,--end-group"

for arg in "$@"; do
    if [ "$arg" = "-c" ]; then
        exec i686-elf-gcc "$@"
    fi
done

out=""
prev=""
for arg in "$@"; do
    if [ "$prev" = "-o" ]; then
        out="$arg"
        break
    fi
    prev="$arg"
done

case "$out" in
    *.a|*.la|*.so|*.o)
        exec i686-elf-gcc "$@"
        ;;
esac

exec i686-elf-gcc "$@" $user_ldflags $user_objs $user_libs
