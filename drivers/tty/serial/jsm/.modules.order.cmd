cmd_drivers/tty/serial/jsm/modules.order := {   echo drivers/tty/serial/jsm/jsm.ko; :; } | awk '!x[$$0]++' - > drivers/tty/serial/jsm/modules.order
