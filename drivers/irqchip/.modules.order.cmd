cmd_drivers/irqchip/modules.order := {  :; } | awk '!x[$$0]++' - > drivers/irqchip/modules.order
