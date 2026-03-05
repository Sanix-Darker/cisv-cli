.PHONY: core cli all test clean
all: core cli
core:
	$(MAKE) -C core all
cli: core
	$(MAKE) -C cli all

test: all
	$(MAKE) -C cli test

clean:
	$(MAKE) -C core clean
	$(MAKE) -C cli clean
