obj-m += aclinedrv.o
obj-m += triac1drv.o
obj-m += triac2drv.o
obj-m += triac3drv.o
obj-m += triac4drv.o

triac1drv-objs := triacdrv.o
triac2drv-objs := triacdrv.o
triac3drv-objs := triacdrv.o
triac4drv-objs := triacdrv.o

ccflags-y := -std=gnu99 -Wall


all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
install:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install
	depmod -A
	
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
