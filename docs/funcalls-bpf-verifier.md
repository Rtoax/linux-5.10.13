
```c
bpf_prog_load() {
  /* 新分配 */
  prog = bpf_prog_alloc(bpf_prog_size(attr->insn_cnt), GFP_USER);
  bpf_check();
  bpf_prog_select_runtime(prog); /* JIT in here */
}
```