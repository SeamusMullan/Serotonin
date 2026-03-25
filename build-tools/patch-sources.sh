#!/bin/bash
# patch-sources.sh — Add Serotonin target support to binutils, GCC, and newlib.

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
BINUTILS_SRC="$DIR/src/binutils-gdb"
GCC_SRC="$DIR/src/gcc"
NEWLIB_SRC="$DIR/../newlib"

# Helper: skip if already patched, warn if file missing
guard() {
    local file="$1" desc="$2"
    if [ ! -f "$file" ]; then
        echo "  [WARN] $file not found, skipping $desc"
        return 1
    fi
    if grep -q 'serotonin' "$file" 2>/dev/null; then
        echo "  [skip] $desc (already patched)"
        return 1
    fi
    return 0
}

# ═══════════════════════════════════════════════════════
# config.sub — add serotonin* to the OS whitelist
# ═══════════════════════════════════════════════════════
patch_config_sub() {
    local file="$1" label="$2"
    guard "$file" "$label config.sub" || return 0

    # Try single-line format first (e.g. | fiwix* ))
    if grep -q '| fiwix\* )' "$file"; then
        sed -i 's/| fiwix\* )/| serotonin* | fiwix* )/' "$file"
    # Try multi-line format (e.g. | fiwix* \)
    elif grep -q '| fiwix\*' "$file"; then
        sed -i '/| fiwix\*/i\\t| serotonin* \\' "$file"
    # Newlib-style: no fiwix, add before the closing *)
    elif grep -q '| emx\*)' "$file"; then
        sed -i 's/| emx\*)/| emx* | serotonin*)/' "$file"
    else
        echo "  [WARN] $label config.sub: no suitable anchor found"
        return 0
    fi
    echo "  [ok]   $label config.sub"
}

echo "=== Patching config.sub ==="
patch_config_sub "$GCC_SRC/config.sub" "GCC"
patch_config_sub "$BINUTILS_SRC/config.sub" "binutils"
patch_config_sub "$NEWLIB_SRC/config.sub" "newlib"

# Patch all remaining config.sub/configfsf.sub files in the GCC tree
# (bundled gettext, gmp, mpfr, mpc, isl each have their own copy)
echo "  Patching bundled config.sub files..."
for f in $(find "$GCC_SRC" -name 'config.sub' -o -name 'configfsf.sub' 2>/dev/null); do
    [ -f "$f" ] || continue
    grep -q 'serotonin' "$f" 2>/dev/null && continue
    if grep -q '| fiwix\* )' "$f"; then
        sed -i 's/| fiwix\* )/| serotonin* | fiwix* )/' "$f"
    elif grep -q '| fiwix\*' "$f"; then
        sed -i '/| fiwix\*/i\\t| serotonin* \\' "$f"
    elif grep -q '| skyos\*' "$f"; then
        sed -i 's/| skyos\*/| serotonin* | skyos*/' "$f"
    elif grep -q '| emx\*)' "$f"; then
        sed -i 's/| emx\*)/| emx* | serotonin*)/' "$f"
    else
        echo "  [WARN] no anchor in: $f"
        continue
    fi
    echo "  [ok]   $(echo "$f" | sed "s|$GCC_SRC/||")"
done

# ═══════════════════════════════════════════════════════
# binutils: bfd/config.bfd
# ═══════════════════════════════════════════════════════
echo ""
echo "=== Patching binutils ==="

F="$BINUTILS_SRC/bfd/config.bfd"
if guard "$F" "bfd/config.bfd"; then
    sed -i '/i\[3-7\]86-\*-elf\* | i\[3-7\]86-\*-rtems/i\  i[3-7]86-*-serotonin*)\n    targ_defvec=i386_elf32_vec\n    targ_selvecs="iamcu_elf32_vec"\n    ;;' "$F"
    echo "  [ok]   bfd/config.bfd"
fi

# gas/configure.tgt
F="$BINUTILS_SRC/gas/configure.tgt"
if guard "$F" "gas/configure.tgt"; then
    sed -i '/i386-\*-elf\*)/i\  i386-*-serotonin*)\t\t\tfmt=elf ;;' "$F"
    echo "  [ok]   gas/configure.tgt"
fi

# ld/configure.tgt
F="$BINUTILS_SRC/ld/configure.tgt"
if guard "$F" "ld/configure.tgt"; then
    sed -i '/i\[3-7\]86-\*-elf\* | i\[3-7\]86-\*-rtems/i\i[3-7]86-*-serotonin*)\n\t\t\ttarg_emul=elf_i386\n\t\t\ttarg_extra_emuls=elf_iamcu\n\t\t\t;;' "$F"
    echo "  [ok]   ld/configure.tgt"
fi

# ═══════════════════════════════════════════════════════
# GCC: gcc/config.gcc
# ═══════════════════════════════════════════════════════
echo ""
echo "=== Patching GCC ==="

F="$GCC_SRC/gcc/config.gcc"
if guard "$F" "gcc/config.gcc"; then
    sed -i '/^i\[34567\]86-\*-elf\*)/i\i[34567]86-*-serotonin*)\n\ttm_file="${tm_file} i386/unix.h i386/att.h elfos.h newlib-stdint.h i386/i386elf.h serotonin.h"\n\t;;' "$F"
    echo "  [ok]   gcc/config.gcc"
fi

# Install serotonin.h into gcc/config/
cp "$DIR/serotonin.h" "$GCC_SRC/gcc/config/serotonin.h"
echo "  [ok]   installed gcc/config/serotonin.h"

# libgcc/config.host
F="$GCC_SRC/libgcc/config.host"
if guard "$F" "libgcc/config.host"; then
    sed -i '/^i\[34567\]86-\*-elf\*)/i\i[34567]86-*-serotonin*)\n\ttmake_file="$tmake_file i386/t-crtstuff t-crtstuff-pic t-libgcc-pic"\n\t;;' "$F"
    echo "  [ok]   libgcc/config.host"
fi

# fixincludes/mkfixinc.sh
F="$GCC_SRC/fixincludes/mkfixinc.sh"
if guard "$F" "fixincludes/mkfixinc.sh"; then
    sed -i '/-\*-vxworks7\*/a\    *-*-serotonin* | \\' "$F"
    echo "  [ok]   fixincludes/mkfixinc.sh"
fi

# ═══════════════════════════════════════════════════════
# newlib: configure.host
# ═══════════════════════════════════════════════════════
echo ""
echo "=== Patching newlib ==="

# newlib doesn't need a special configure.host entry for serotonin:
# with --disable-newlib-supplied-syscalls, the default (no sys_dir) is correct.
echo "  [skip] newlib/configure.host (default config is sufficient)"

echo ""
echo "=== All patches applied ==="
