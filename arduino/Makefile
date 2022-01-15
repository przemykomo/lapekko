# nano clone https://bbs.archlinux.org/viewtopic.php?id=269975
all:
	mkdir -p $(PWD)/preprocessed
	g++ -x c++ -E -P arduino.ino > $(PWD)/preprocessed/preprocessed.ino
	arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old $(PWD)/preprocessed/
	arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:nano:cpu=atmega328old $(PWD)/preprocessed/

leonardo:
	mkdir -p $(PWD)/preprocessed
	g++ -x c++ -E -P arduino.ino > $(PWD)/preprocessed/preprocessed.ino
	arduino-cli compile --fqbn arduino:avr:leonardo $(PWD)/preprocessed/
	arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:leonardo $(PWD)/preprocessed/

clean:
	rm -r preprocessed
