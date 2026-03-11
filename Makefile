.PHONY: update-core core cli all test clean
all: core cli
update-core:
	git submodule update --init --remote --recursive core
core:
	$(MAKE) -C core/core all
cli: core
	$(MAKE) -C cli all

test: all
	$(MAKE) -C cli test

clean:
	$(MAKE) -C core/core clean
	$(MAKE) -C cli clean
