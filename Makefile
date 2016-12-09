.PHONY: all clean archive

obj-m += xppen.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

archive:
	tar f - --exclude=.* -C ../ -c hanvon | gzip -c9 > ../xppen-`date +%Y%m%d`.tgz
