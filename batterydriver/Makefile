obj-m += lapekko_battery_module.o

ccflags-y=-DARDUINO_NANO

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

compile_commands:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) compile_commands.json
