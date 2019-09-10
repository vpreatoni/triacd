obj-m += aclinedrv.o
obj-m += triac1drv.o
obj-m += triac2drv.o
obj-m += triac3drv.o
obj-m += triac4drv.o

triac1drv-objs := triacdrv.o
triac2drv-objs := triacdrv.o
triac3drv-objs := triacdrv.o
triac4drv-objs := triacdrv.o

ccflags-y := -std=gnu99 -Wno-declaration-after-statement 


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
