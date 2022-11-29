/**
 * 1.
 * $ clang -target bpf -emit-llvm -S 32-bit-example.c
 * $ llc -march=bpf 32-bit-example.ll
 * $ llc -march=bpf -mattr=+alu32 32-bit-example.ll
 * $ cat 32-bit-example.s
 */
void cal(unsigned int *a, unsigned int *b, unsigned int *c)
{
	unsigned int sum = *a + *b;
	*c = sum;
}
