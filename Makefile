obj-m += mydriver.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
ccflags-y := -I$(PWD)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	
#	sudo rmmod mydriver
#	sudo insmod mydriver.ko

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

