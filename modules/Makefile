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

# Default to running kernel's build directory if KDIR not set externally
KDIR ?= "/lib/modules/$(shell uname -r)/build"

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
	
install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install
	depmod -A
	
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/triac?drv.ko
	rm -f /lib/modules/$(shell uname -r)/extra/aclinedrv.ko
	depmod -A
	
