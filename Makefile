
# Grand Unified Makefile for Xen.
#
# Builds everything except Xenolinux:
#  cd xenolinux-<version>-sparse
#  ./mkbuildtree <build dir>
#  cd <build dir>
#  ARCH=xeno make oldconfig
#  ARCH=xeno make dep
#  ARCH=xeno make bzImage
#  (<build dir> should be a vanilla linux tree with matching version)

all:	
	$(MAKE) -C xen
	$(MAKE) -C tools

install: all
	$(MAKE) -C xen install
	$(MAKE) -C tools install

dist: all
	$(MAKE) prefix=`pwd`/../install -C xen install
	$(MAKE) prefix=`pwd`/../install -C tools install

clean:
	$(MAKE) -C xen clean
	$(MAKE) -C tools clean
