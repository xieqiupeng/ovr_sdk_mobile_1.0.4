# TODO, just combine the Mac and Linux makefiles... but for now just include them based on current platform.

HOST_OS = $(shell uname -s | tr \'[:upper:]\' \'[:lower:]\')

ifeq ($(HOST_OS),darwin)
	include Makefile_Mac
else
	include Makefile_Linux
endif