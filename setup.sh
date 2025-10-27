#!/bin/bash
# This file is used to download the requirements for creating a Cross Compiler. Namely it downloads:
# - Binutils (from the git repository)[git://sourceware.org/git/binutils-gdb.git]
# - GCC (from the git repository)[git://gcc.gnu.org/git/gcc.git]
#
# Into the correct directories for this operating system project to function.
# The files get places in the build-tools directory from which the build-tools.sh script can be run to build and install both tools.
#
# Note for MacOS, install the latest version of libiconv or build it yourself. This script will NOT do this.
#
# For info on making a cross compiler, please read https://wiki.osdev.org/GCC_Cross-Compiler

set -e

# check if we are running from the root directory, any other directory will not work
# simple check for a gitignore and the docs folder, since there shouldnt be either anywhere else
if [ ! -d "build-tools" ] || [ ! -d "docs" ]; then
    echo "Error: This script must be run from the root directory of the project."
    echo "Expected directory structure: The root directory should contain a 'build-tools' folder."
    echo "Please navigate to the root directory of the project and try running the script again."
    exit 1
fi

# test implementation before formatting, error checking and other stuff
rm -rf build-tools/binutils-gdb build-tools/gcc # cleanup
mkdir -p build-tools

git clone git://sourceware.org/git/binutils-gdb.git build-tools/src/binutils-gdb
git clone git://gcc.gnu.org/git/gcc.git build-tools/src/gcc

echo "=== Download complete! ==="
echo "Binutils and GCC have been downloaded to the 'build-tools' directory."
echo "You can now run 'build-tools/build-tools.sh' to build and install the cross compiler."
