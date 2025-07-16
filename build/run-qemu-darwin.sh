qemu-img create -f raw serotonin.img 1G
qemu-system-x86_64 -m 2048 -boot d -cdrom serotonin.iso -vga std -drive file=serotonin.img,if=ide,index=1,media=disKaterrak