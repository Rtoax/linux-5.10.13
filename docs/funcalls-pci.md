pcie subsys
===========

## subsys_initcall(pci_subsys_init)

```
pci_subsys_init() {
  if (x86_init.pci.init()) {
    pci_legacy_init() {
      pcibios_scan_root(0) {
        pci_scan_root_bus();
        ...
      }
    }
  }
}
```
