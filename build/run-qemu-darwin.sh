<<<<<<< HEAD
qemu-img create -f raw serotonin.img 1G
qemu-system-x86_64 -m 2048 -boot d -cdrom serotonin.iso -vga std -drive file=serotonin.img,if=ide,index=1,media=disKaterrak
=======
#!/bin/bash

# --- Configuration ---
IMG_FILE="serotonin.img"
IMG_SIZE_MB=1024 # Size in MB
VOLUME_NAME="SEROTONIN"

# --- Create the .img file ---
echo "Creating $IMG_FILE with size ${IMG_SIZE_MB}MB..."
dd if=/dev/zero of="$IMG_FILE" bs=1m count="$IMG_SIZE_MB"

# --- Attach the .img file ---
echo "Attaching disk image..."
DEVICE=$(hdiutil attach -nomount "$IMG_FILE" | awk '{print $1}')
echo "Attached to $DEVICE"

# --- Format it to FAT32 ---
echo "Formatting $DEVICE to FAT32..."
sudo diskutil eraseDisk FAT32 "$VOLUME_NAME" MBRFormat "$DEVICE"

# --- Detach the image ---
echo "Detaching $DEVICE..."
hdiutil detach "$DEVICE"

echo "Done. $IMG_FILE is now formatted as FAT32."


qemu-system-x86_64 -m 2048 -boot d -cdrom serotonin.iso -vga std -drive file=serotonin.img,if=ide,index=1,media=disk
>>>>>>> 613a40898fd49988be36b29f33753fb545e3def4
