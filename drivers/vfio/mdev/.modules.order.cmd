cmd_drivers/vfio/mdev/modules.order := {   echo drivers/vfio/mdev/mdev.ko;   echo drivers/vfio/mdev/vfio_mdev.ko; :; } | awk '!x[$$0]++' - > drivers/vfio/mdev/modules.order
