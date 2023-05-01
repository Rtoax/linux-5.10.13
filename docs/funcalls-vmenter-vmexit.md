VMENTER/VMEXIT
=================


# VMX


## VMENTER

```
ioctl(vcpufd, KVM_RUN, ...)
  kvm_arch_vcpu_ioctl_run()
    vcpu_run()
      vcpu_enter_guest()
        vmx_vcpu_run() = kvm_x86_ops.run(vcpu);
          vmx_vcpu_enter_exit()
            __vmx_vcpu_run(): ASM
              加载 Guest 寄存器
              vmx_vmenter(): ASM
                vmlaunch [指令]
              保存 Guest 寄存器
```

## VMEXIT

```
            vmx->fail = __vmx_vcpu_run()
          vmx_vcpu_enter_exit()
          vmx->exit_reason = vmcs_read32(VM_EXIT_REASON)
        exit_fastpath = vmx_vcpu_run()
      vcpu_enter_guest()
    vcpu_run()
  kvm_arch_vcpu_ioctl_run()
kvm_vcpu_ioctl()
ioctl(vcpufd, KVM_RUN, ...)
```


# Links

- https://www.felixcloutier.com/x86/
- https://www.felixcloutier.com/x86/vmlaunch:vmresume
