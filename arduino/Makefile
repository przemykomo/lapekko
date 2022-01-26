# todo: automate getting the cached core.a file
all:
	/home/przemek/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-g++ -c -g -Os -w -std=gnu++11 -fpermissive -fno-exceptions -ffunction-sections -fdata-sections -fno-threadsafe-statics -Wno-error=narrowing -MMD -flto -mmcu=atmega328p -DF_CPU=16000000L -DARDUINO=10607 -DARDUINO_AVR_NANO -DARDUINO_ARCH_AVR -I/home/przemek/.arduino15/packages/arduino/hardware/avr/1.8.4/cores/arduino -I/home/przemek/.arduino15/packages/arduino/hardware/avr/1.8.4/variants/eightanaloginputs -I/home/przemek/.arduino15/packages/arduino/hardware/avr/1.8.4/libraries/EEPROM/src $(PWD)/arduino.cpp -o $(PWD)/arduino.cpp.o
	# compiling core
	/home/przemek/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-gcc -w -Os -g -flto -fuse-linker-plugin -Wl,--gc-sections -mmcu=atmega328p -o $(PWD)/arduino.cpp.elf $(PWD)/arduino.cpp.o $(PWD)/core.a -lm
	/home/przemek/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-objcopy -O ihex -j .eeprom --set-section-flags=.eeprom=alloc,load --no-change-warnings --change-section-lma .eeprom=0 $(PWD)/arduino.cpp.elf $(PWD)/arduino.cpp.eep
	/home/przemek/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-objcopy -O ihex -R .eeprom $(PWD)/arduino.cpp.elf $(PWD)/arduino.cpp.hex
	# upload
	arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:nano:cpu=atmega328old -i $(PWD)/arduino.cpp.hex

# nano clone https://bbs.archlinux.org/viewtopic.php?id=269975
nano: preprocess
	arduino-cli compile -v --fqbn arduino:avr:nano:cpu=atmega328old $(PWD)/preprocessed/
	arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:nano:cpu=atmega328old $(PWD)/preprocessed/

leonardo: preprocess
	arduino-cli compile --fqbn arduino:avr:leonardo $(PWD)/preprocessed/
	arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:leonardo $(PWD)/preprocessed/

preprocess:
	mkdir -p $(PWD)/preprocessed
	g++ -x c++ -I~/.arduino15/packages/arduino/hardware/avr/1.8.4/libraries/EEPROM/src -E -P arduino.ino > $(PWD)/preprocessed/preprocessed.ino

clean:
	rm -r preprocessed
	rm *.d *.eep *.elf *.hex *.o
