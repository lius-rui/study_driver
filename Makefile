KVERS = $(shell uname -r)

obj-m += globalmem.o
obj-m += interrupt.o
obj-m += platform_second.o
obj-m += misc.o

build: kernel_modules

kernel_modules:
	make -C /lib/modules/$(KVERS)/build M=$(CURDIR) modules

clean:
	make -C /lib/modules/$(KVERS)/build M=$(CURDIR) clean

