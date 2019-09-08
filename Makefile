obj-m := triacdrv.o

ccflags-y := -std=gnu99 -Wno-declaration-after-statement -Wno-unused-result
MY_CFLAGS += -g -DDEBUG
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
debug:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 
	EXTRA_CFLAGS="$(MY_CFLAGS)"

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
