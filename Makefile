# Top-level convenience Makefile: everything lives in campuscompute/.
#   make         build the Shire Shell and test drivers
#   make test    run every automated test suite
#   make clean   remove build output
.PHONY: all test sanitize clean
all test sanitize clean:
	$(MAKE) -C campuscompute $@
