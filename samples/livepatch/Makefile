obj-m += livepatch-callbacks-busymod.o
obj-m += livepatch-callbacks-demo.o
obj-m += livepatch-callbacks-mod.o
obj-m += livepatch-sample.o
obj-m += livepatch-shadow-fix1.o
obj-m += livepatch-shadow-fix2.o
obj-m += livepatch-shadow-mod.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean