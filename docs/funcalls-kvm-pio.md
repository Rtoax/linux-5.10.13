KVM PIO
=========


# PIO
```c
handle_io() = kvm_vmx_exit_handlers[EXIT_REASON_IO_INSTRUCTION]
  vmx_get_exit_qual() <从 vmcs 读取 exit_qualification>
    if string
      kvm_emulate_instruction()
    else
      kvm_fast_pio()
```

