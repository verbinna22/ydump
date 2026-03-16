obj-m += dmp.o

# membuf-objs := main.o array_entry.o allocated_devices.o device_size.o module_params.o module_operations.o

all:
		make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
		make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean