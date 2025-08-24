echo building user init program 
../build-tools/bin/bin/i686-elf-gcc -m32 -ffreestanding -nostdlib -nostartfiles -Wl,-Ttext=0x400100 -o ../user/init/init.elf ../user/init/init.c

echo copying to disk
echo modprobe nbd
sudo modprobe nbd
echo qemu-nbd
sudo qemu-nbd --connect=/dev/nbd1 /var/lib/libvirt/images/serotonin-1.qcow2
echo mounting
sudo mount /dev/nbd1p1 /mnt
echo copying init.elf
sudo cp ../user/init/init.elf /mnt/bin/init
echo unmounting
sudo umount /dev/nbd1p1
echo disconnecting
sudo qemu-nbd --disconnect /dev/nbd1
echo finished