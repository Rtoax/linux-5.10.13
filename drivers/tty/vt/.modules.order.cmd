cmd_drivers/tty/vt/modules.order := {  :; } | awk '!x[$$0]++' - > drivers/tty/vt/modules.order
