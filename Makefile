KBUILD_CFLAGS += -Wall -Werror
obj-m := bbfs.o
bbfs-objs := dir.o file.o fs.o inode.o super.o
CURRENT_PATH := $(shell pwd)
LINUX_KERNEL := $(shell uname -r)
LINUX_KERNEL_PATH := /usr/src/linux-headers-$(LINUX_KERNEL)
MKFS = mkfs.bbfs

all: $(MKFS)
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules

$(MKFS): mkfs.c
	$(CC) -O2 -Wall -o $@ $<

clean:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) clean
	rm $(MKFS)
