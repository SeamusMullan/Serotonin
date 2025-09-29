#!tbin/bash


# This script is used to set up the virtual disk for the operating system
# The script should set up a simple FAT32 formatted disk containing the user ELF binaries.

# Arguments





# Get Operating System type (used to make sure we make the right kinda file)

OS_TYPE="$(uname)"

