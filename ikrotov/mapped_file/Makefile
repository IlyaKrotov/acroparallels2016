MAKE_DIR := $(PWD)/out
DIRS := List hash_table mapped_file chunk_manager#tester
BUILDDIRS := $(DIRS:%=build-%)
INSTALLDIRS := $(DIRS:%=install-%)
CLEANDIRS := $(DIRS:%=clean-%)
TESTDIRS := $(DIRS:%=test-%)

ifdef DEBUG
#CFLAGS +=  -DDEBUG_LOG -DDEBUG_STDERRLOG -DMEMORY_DEBUG
else
CFLAGS += -Ofast -s -std=c99 #-DNDEBUG
endif
export CFLAGS MAKE_DIR

all: make_dir_init $(BUILDDIRS)
	ar rcs "$(PWD)/out/libmappedfile.a" "$(PWD)/out"/*.o

make_dir_init:
	mkdir -p "$(MAKE_DIR)"

$(DIRS): $(BUILDDIRS)
$(BUILDDIRS):
	$(MAKE) -C $(@:build-%=%)

clean: make_dir_init $(CLEANDIRS)
	rm -rf "$(PWD)/out/"*.o
	rm -rf "$(PWD)/out/libmappedfile.a"

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

test: make_dir_init all $(TESTDIRS)
$(TESTDIRS):
	$(MAKE) -C $(@:test-%=%) test
