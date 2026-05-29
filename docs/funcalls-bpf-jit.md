
# 系统调用加载程序，如 trace.bpf.o

```c
bpf(int cmd, union bpf_attr uattr, size) {
  switch (cmd) {
    case BPF_PROG_LOAD:
      err = bpf_prog_load(&attr, uattr);
      break;
  }
}
```

# 为 bpf 程序分配内存，此时为 bpf 指令

```c
bpf_prog_load() {
  /* 新分配 */
  prog = bpf_prog_alloc(bpf_prog_size(attr->insn_cnt), GFP_USER);
  bpf_prog_select_runtime(prog) {
    bpf_int_jit_compile(prog) {
      bpf_prog_alloc_jited_linfo(prog);
      bpf_int_jit_compile(prog); /* 架构相关函数 */
    }
  }
}
```

```c
bpf_int_jit_compile(prog) {
  /* aarch64 */
  bpf_jit_binary_alloc(image_size, ...) {
    size = round_up(image_size + sizeof(*hdr) + 128, PAGE_SIZE);
    pages = size / PAGE_SIZE;
    bpf_jit_charge_modmem(pages) {
      /* 检查: bpf_jit_current 累加后和 bpf_jit_limit 比较 */
      if (atomic_long_add_return(pages, &bpf_jit_current) >
           (bpf_jit_limit >> PAGE_SHIFT)) {           /* <<< 东风项目此处超限返回失败 >>> */
      }
    }
    /* 检查通过后，分配内存 */
    hdr = bpf_jit_alloc_exec(size) {
      module_alloc(size);
    }
  }
  /* 执行 jit 翻译 */
  build_body(jit_ctx) {
    for (i = 0; i < prog->len; i++) {
      build_insn(insn) {
        /* 例如: BPF_ADD -> aarch64 add */
      }
    }
  }
}
```

```c
bpf_int_jit_compile(prog) {
  /* x86_64 */
  do_jit(prog, ...) {
    for (i = 1; i <= insn_cnt; i++) {
      /* BPF_ADD -> x86 add */
    }
  }
}
```