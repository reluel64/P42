clear
qemu-system-x86_64 -machine q35,accel=kvm -cpu Icelake-Server,+x2apic -smp 1 \
-drive format=raw,file='/home/alex/disk.img' -m 26G       \
-no-reboot                                                           \
-no-shutdown                                                         \
-chardev stdio,mux=on,id=char0                                       \
-mon chardev=char0,mode=readline                                     \
-serial chardev:char0                                                \
-serial chardev:char0                                                \
-usb                                                                 \
-device usb-ehci,id=ehci                                             \
-device qemu-xhci,id=xhci           
#qemu-system-x86_64 -machine q35,accel=kvm -cpu qemu64,+x2apic -smp 8 \

#clear
#qemu-system-x86_64 -machine q35 -cpu Icelake-Server,+x2apic -smp 32,sockets=4,maxcpus=32 \
#-numa node,nodeid=0 \
#-numa node,nodeid=1 \
#-numa node,nodeid=2 \
#-numa node,nodeid=3 \
#-numa cpu,node-id=0,socket-id=0 \
#-numa cpu,node-id=1,socket-id=1 \
#-numa cpu,node-id=2,socket-id=2 \
#-numa cpu,node-id=3,socket-id=3 \
#-drive format=raw,file='/mnt/F0F0492EF048FBFA/Disk Image of sdc (2020-04-22 2025).img' -m 16G \
#-no-reboot \
#-no-shutdown \
#-monitor stdio
#-chardev stdio,mux=on,id=char0 \
#-mon chardev=char0,mode=readline \
#-serial chardev:char0 \
#-serial chardev:char0 \


