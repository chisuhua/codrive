# run "make"
#
ifeq ($(KERNELRELEASE),)

	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

modules:
	$(MAKE) $(MACROS) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C (KERNELDIR) M=$(PWD) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

.PHONY: modules modules_install clean

else

	KBUILD_EXTRA_SYMBOLS := $(PWD)/Module.symvers
	ccflags-y += -I$(src)/.. -I$(src)/include
	opu-y += opu_chardev.o
	opu-x += opu_chardev.o	\
			opu_acpi.o		\
			opu_core.o		\
			opu_chip.o		\
			opu_kms.o		\
			opu_kernel_queue.o		\
			opu_device_queue_manager.o		\
			opu_process_queue_manager.o		\
			opu_core_asic_v1.o		\
			opu_deps.o		\
			opu_fence.o		\
			opu_drv.o		\
			opu_crat.o		\
			opu_dbgdev.o	\
			opu_dbgmgr.o	\
			opu_device.o	\
			opu_doorbell.o	\
			opu_events.o	\
			opu_flat_memory.o	\
			opu_irq.o	\
			opu_iommu.o		\
			opu_migrate.o	\
			opu_module.o	\
			opu_pasid.o		\
			opu_process.o	\
			opu_queue.o		\
			opu_svm.o		\
			opu_topology.o
	obj-m := opu.o
endif

TEST_SRC = $(wildcard tests/test_*.cpp opusmi/device.cpp)
TEST_DIR = ${notdir $(TEST_SRC)}
TEST_OBJ = $(patsubst %.cpp, %.o, $(TEST_SRC))

CXXINCLUDE = -I./ -I../../include -I../include -I./tests -I./opusmi

EXTRA_CFLAGS = -g -mpreferred-stack-boundary=4 -Wall -Werror

RELEASE ?=  1
ifeq ($(RELEASE), 1)
	EXTRA_CFLAGS += -DRELEASE
endif

ifeq ($(VERBOSE), 1)
	EXTRA_CFLAGS += -DVERBOSE
endif

ifeq ($(VERBOSE), 1)
	EXTRA_CFLAGS += -DVVERBOSE
endif

%.o: %.cpp
	$(CXX) -std=c++11 ${CXXINCLUDE} -c $< -o $@

.PHONY: test

test: $(TEST_OBJ)
	$(CXX) -std=c++11 -o tests/test $(TEST_OBJ) /usr/src/gtest/build/libgtest.a -lpthread
	tests/test





