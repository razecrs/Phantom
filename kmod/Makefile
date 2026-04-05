# Phantom kernel module — Kbuild Makefile
#
# I build this as an LKM for KernelSU Next on Android GKI (kernel 5.10+).
# Two ways to build:
#
#   1. Against a pre-built kernel tree (fastest, works with GKI):
#      make KDIR=/path/to/android-kernel/out/android13-5.10/dist \
#           ARCH=arm64 CROSS_COMPILE=aarch64-linux-android-
#
#   2. Using Android kernel source + KernelSU Next DDK (recommended):
#      See docs/building-kmod.md
#
# After build: push phantom_kmod.ko to /data/phantom/ and run:
#   insmod /data/phantom/phantom_kmod.ko
# Or wire it into the KernelSU module post-fs-data.sh for auto-load.

KDIR  ?= /lib/modules/$(shell uname -r)/build
ARCH  ?= arm64

# sources
obj-m += phantom_kmod.o
phantom_kmod-objs := src/phantom_kmod.o

# I want to be verbose about what symbols I'm probing at load time
ccflags-y := -O2 -DDEBUG_PHANTOM

all:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	    ARCH=$(ARCH) \
	    CROSS_COMPILE=$(CROSS_COMPILE) \
	    modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# push and insert on connected device (requires adb root)
deploy:
	adb push phantom_kmod.ko /data/phantom/phantom_kmod.ko
	adb shell "insmod /data/phantom/phantom_kmod.ko && echo 'loaded ok'"

# check dmesg for my log lines after load
check:
	adb shell "dmesg | grep phantom"

.PHONY: all clean deploy check
