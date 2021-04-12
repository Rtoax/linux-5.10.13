cmd_drivers/iommu/intel/modules.order := {  :; } | awk '!x[$$0]++' - > drivers/iommu/intel/modules.order
