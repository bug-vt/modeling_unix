qemu --display curses \
    -drive file=usbdisk.img,format=raw,index=0,media=disk \
    -m 4 \
    -smp cpus=4,cores=1,threads=1,sockets=4 \
    -machine type=pc,accel=kvm
