cmd_drivers/vfio/mdev/mdev.ko := ld -r -m elf_x86_64  --build-id=sha1  -T scripts/module.lds -o drivers/vfio/mdev/mdev.ko drivers/vfio/mdev/mdev.o drivers/vfio/mdev/mdev.mod.o;  true
