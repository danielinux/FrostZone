# Top‑level build  Makefile
#
#This Makefile delegates to the individual component build systems (e.g.
#`secure-supervisor`, `frosted`).  It accepts a ``TARGET`` variable that
#controls which architecture‑specific files are used.
#
#Usage:
#```
#make TARGET=rp2350   # Should give a warning to use CMake from arch/rp2350 instead
#make TARGET=stm32h563 #default
#```
#
#The build produces the ``secure.elf`` and ``frosted.elf`` binaries
#located in the respective component directories.
#"""

ifdef TARGET
else
TARGET := stm32h563
endif

all: secure-supervisor frostzone

clean: clean-secure clean-frosted

# Secure supervisor
secure-supervisor:
	@echo "Building secure supervisor for $(TARGET)"
	$(MAKE) -C secure-supervisor TARGET=$(TARGET)

clean-secure:
	$(MAKE) -C secure-supervisor clean

frostzone:
	@make -C secure-supervisor
	@echo "Building FrostZone kernel"
	$(MAKE) -C frosted

clean-frosted:
	$(MAKE) -C frosted clean

PHONY: all clean secure-supervisor clean-secure frostzone clean-frosted
