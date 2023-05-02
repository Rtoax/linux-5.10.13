KVM NMI
========


# EXIT_REASON_EXCEPTION_NMI

```
ioctl(vcpufd, KVM_RUN, ...)
  kvm_arch_vcpu_ioctl_run()
    vcpu_run()
      vcpu_enter_guest()
        exit_fastpath = vmx_vcpu_run()
        ...
        vmx_handle_exit(vcpu, exit_fastpath) == kvm_x86_ops.handle_exit
          handle_exception_nmi()=kvm_vmx_exit_handlers[EXIT_REASON_EXCEPTION_NMI]
```

下面是在 vcpu_enter_guest() 中调用的

```
vcpu_enter_guest()
  vmx_x86_ops.handle_exit_irqoff = vmx_handle_exit_irqoff()
  vmx_handle_exit_irqoff()
    handle_exception_nmi_irqoff()
      if is_page_fault()
        ...
      if is_machine_check()
        ...
      if is_nmi()
        ...
```


# #UD

> #UD 目前会因为没有设置 EmulateOnUD 而直接返回模拟失败。

```
handle_exception_nmi()
  handle_ud()
    kvm_emulate_instruction()
      x86_emulate_instruction()
        x86_decode_insn()
        x86_emulate_insn()
```
