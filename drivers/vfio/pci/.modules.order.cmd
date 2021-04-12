cmd_drivers/vfio/pci/modules.order := {   echo drivers/vfio/pci/vfio-pci.ko; :; } | awk '!x[$$0]++' - > drivers/vfio/pci/modules.order
