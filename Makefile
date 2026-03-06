.PHONY: core cli all test clean
all: core cli
core:
	$(MAKE) -C core/core all
cli: core
	$(MAKE) -C cli all

test: all
	$(MAKE) -C cli test

clean:
	$(MAKE) -C core/core clean
	$(MAKE) -C cli clean
