BPF Helpers Function Call
=========================


# 以 BPFTrace kprobe 程序中的 `numaid` builtin 为例

## 内核中的 bpf_verifier_ops

```c
/**
 * 这里传入的 attr 是已经获取到 prog->type 是 bpf_attr 指定的。
 */
static int bpf_prog_load(union bpf_attr *attr, union bpf_attr __user *uattr)
{
    ...
	err = bpf_check(&prog, attr, uattr);
    ...
}

struct bpf_verifier_env {
    ...
	const struct bpf_verifier_ops *ops;
    ...
};

int bpf_check(struct bpf_prog **prog, union bpf_attr *attr,
	      union bpf_attr __user *uattr)
{
    ...
	env->ops = bpf_verifier_ops[env->prog->type];
    ...
}

static const struct bpf_verifier_ops * const bpf_verifier_ops[] = {
    ...
	[BPF_PROG_TYPE_KPROBE] = & kprobe_verifier_ops,
    ...
};

const struct bpf_verifier_ops kprobe_verifier_ops = {
	.get_func_proto  = kprobe_prog_func_proto,
	.is_valid_access = kprobe_prog_is_valid_access,
};

static const struct bpf_func_proto *
kprobe_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
    ...
	default:
		return bpf_tracing_func_proto(func_id, prog);
	}
}

const struct bpf_func_proto *
bpf_tracing_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
    ...
	case BPF_FUNC_get_numa_node_id:
		return &bpf_get_numa_node_id_proto;
    ...
	}
}

BPF_CALL_0(bpf_get_numa_node_id)
{
	return numa_node_id();
}

const struct bpf_func_proto bpf_get_numa_node_id_proto = {
	.func		= bpf_get_numa_node_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};
```


