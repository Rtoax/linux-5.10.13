cmd_drivers/tty/serial/8250/modules.order := {  :; } | awk '!x[$$0]++' - > drivers/tty/serial/8250/modules.order
