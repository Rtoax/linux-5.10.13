EXIT_REASON_CPUID
=================


1. 虚拟机中调用 cpuid 指令；
2. 硬件检测到这个指令，并将 EXIT_REASON_CPUID 保存到 VMCS 的 VM_EXIT_REASON 区域；
3. 然后退出进行处理；


# EXIT_REASON_CPUID

```
ioctl(vcpufd, KVM_RUN, ...)
  kvm_arch_vcpu_ioctl_run()
    vcpu_run()
      vcpu_enter_guest()
        exit_fastpath = vmx_vcpu_run()
        ...
        vmx_handle_exit(vcpu, exit_fastpath) == kvm_x86_ops.handle_exit
          kvm_emulate_cpuid() == kvm_vmx_exit_handlers[EXIT_REASON_CPUID]
            kvm_cpuid()
```