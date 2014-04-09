obj-m := wdt3610.o

default:
	make -C /lib/modules/"`uname -r`"/build SUBDIRS="`pwd`" modules
