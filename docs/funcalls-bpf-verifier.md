
```c
bpf_prog_load() {
  /* 新分配 */
  prog = bpf_prog_alloc(bpf_prog_size(attr->insn_cnt), GFP_USER);
  bpf_check(); /* Verifier in here */
  bpf_prog_select_runtime(prog); /* JIT in here */
}
```

# linux 5.10.13

```c
bpf_check() {
  check_subprogs() {
    add_subprog(env, off) {
      env->subprog_info[env->subprog_cnt++].start = off;
    }
    for (i = 0; i < insn_cnt; i++) {
      if (insn[i].code != (BPF_JMP | BPF_CALL))
        continue;
      if (insn[i].src_reg != BPF_PSEUDO_CALL)
        continue;

      add_subprog(env, i + insn[i].imm + 1);
    }

    // 检查是否有乱 jmp，是否缺失 jmp/exit
    for (i = 0; i < insn_cnt; i++) {
    }
  }

  do_check_subprogs(env) {
    for (i = 1; i < env->subprog_cnt; i++) {
      do_check_common(env, i) {
        do_check(env); /* 细致检查合法性 */
      }
    }
  }
  do_check_main(); // TODO
}
```

## do_check()

```c
do_check(env) {
  struct bpf_insn *insns = env->prog->insnsi;
  for (;;) {
    struct bpf_insn *insn = &insns[env->insn_idx];
    u8 class = BPF_CLASS(insn->code);

    if (class == BPF_ALU || class == BPF_ALU64) {
      check_alu_op(env, insn);
    } else if (class == BPF_LDX) {
      check_reg_arg(env, insn->src_reg, SRC_OP);
      check_reg_arg(env, insn->dst_reg, DST_OP_NO_MARK);
      check_mem_access(env, env->insn_idx, insn->src_reg,
                        insn->off, BPF_SIZE(insn->code),
                        BPF_READ, insn->dst_reg, false);
      // ...
    } else if (class == BPF_STX) {
      check_reg_arg(env, insn->src_reg, SRC_OP);
      check_reg_arg(env, insn->dst_reg, SRC_OP);
      check_mem_access(...);
      // ...
    } else if (class == BPF_ST) {
      check_reg_arg(env, insn->dst_reg, SRC_OP);
      check_mem_access(...);
    } else if (class == BPF_JMP || class == BPF_JMP32) {
      u8 opcode = BPF_OP(insn->code);
      if (opcode == BPF_CALL) {
        if (insn->src_reg == BPF_PSEUDO_CALL)
          err = check_func_call(env, insn, &env->insn_idx);
        else
          err = check_helper_call(env, insn->imm, env->insn_idx);
      } else if (opcode == BPF_JA) {
        // ...
      } else if (opcode == BPF_EXIT) {
        check_reference_leak(env);
        check_return_code(env);
        // ...
      }
    } else if (class == BPF_LD) {
      if (mode == BPF_ABS || mode == BPF_IND) {
        check_ld_abs(env, insn);
      } else if (mode == BPF_IMM) {
        check_ld_imm(env, insn);
      }
    }

    env->insn_idx++;
  }
}
```