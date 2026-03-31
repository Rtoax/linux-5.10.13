

# start_kernel()

```c
start_kernel() {
  fs_initcall(firmware_class_init)
}
```

# firmware_class_init()

```c
firmware_class_init() {
  register_sysfs_loader() {
    // /sys/class/firmware/
  }
}
```

# request_firmware()

```c
// see https://github.com/NVIDIA/open-gpu-kernel-modules.git
request_firmware(..., "nvidia/" NV_VERSION_STRING "/gsp_log_ga10x.bin", ...) {
  _request_firmware() {
    _request_firmware_prepare() {
    }
    fw_get_filesystem_firmware() {
      // /lib/firmware
      for (path : fw_path[]) {
        kernel_read_file_from_path_initns(path) {
	}
      }
    }
  }
}
```
