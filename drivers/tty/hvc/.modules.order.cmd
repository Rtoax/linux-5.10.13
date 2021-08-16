cmd_drivers/tty/hvc/modules.order := {  :; } | awk '!x[$$0]++' - > drivers/tty/hvc/modules.order
