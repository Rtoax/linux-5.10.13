KVM MMIO
=========


# APIC

## LAPIC Write

```c
handle_apic_access() = kvm_vmx_exit_handlers[EXIT_REASON_APIC_ACCESS]
  kvm_emulate_instruction()
    x86_emulate_instruction()
      x86_decode_insn()
      x86_emulate_insn()
        writeback()
          case OP_MEM:
            segmented_write()
              ctxt->ops->write_emulated()=emulator_write_emulated()
              emulator_write_emulated()
                emulator_read_write() 'write_emultor.read_write_mmio = write_mmio()'
                  emulator_read_write_onepage()
                    write_mmio() 'ops->read_write_mmio()'
                      vcpu_mmio_write()
                        kvm_iodevice_write()
                          apic_mmio_write() 'dev->ops->write()'
                            kvm_lapic_reg_write()
                              case APIC_ICR:
                                kvm_apic_send_ipi()
```

