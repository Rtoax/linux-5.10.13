cmd_drivers/vfio/pci/vfio-pci.ko := ld -r -m elf_x86_64  --build-id=sha1  -T scripts/module.lds -o drivers/vfio/pci/vfio-pci.ko drivers/vfio/pci/vfio-pci.o drivers/vfio/pci/vfio-pci.mod.o;  true
