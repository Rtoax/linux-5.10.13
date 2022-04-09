/**
 *  见 arch/x86/boot/compressed/vmlinux.lds.S 中
 *  `.rodata..compressed`
 */
.section ".rodata..compressed","a",@progbits
.globl z_input_len
z_input_len = 13449128  //压缩的`vmlinux.bin.gz`的大小
.globl z_output_len
z_output_len = 83868872 //解压后的`vmlinux.bin.gz`的大小
.globl input_data, input_data_end
input_data:
.incbin "arch/x86/boot/compressed/vmlinux.bin.gz"
input_data_end: //`input_data_end`指向压缩的Linux镜像的末尾
.section ".rodata","a",@progbits
.globl input_len
input_len:
	.long 13449128
.globl output_len
output_len:
	.long 83868872
