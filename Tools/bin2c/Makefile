help:
	@echo Usage....
	@echo "make help           - displays this text"
	@echo "make clean          - delete intermediate files"
	@echo "make all -j20       - build bin2c"
	@echo "make run -j20       - build and run bin2c"
	@echo "make run_debug -j20 - build and run inside debugger"


HOST_OS          = $(shell uname -s | tr \'[:upper:]\' \'[:lower:]\')
HOST_ARCH        = $(shell uname -m | tr \'[:upper:]\' \'[:lower:]\')
HOST_PLATFORM    = $(HOST_OS)_$(HOST_ARCH)

BRANCH_ROOT      = $(realpath ../..)
PROJECT_ROOT     = $(BRANCH_ROOT)/Tools/bin2c
VENDOR_ROOT      = $(BRANCH_ROOT)/3rdParty
VRLIB_ROOT       = $(BRANCH_ROOT)/VrAppFramework

BIN_ROOT         = $(PROJECT_ROOT)/bin/$(HOST_PLATFORM)
OBJ_ROOT         = $(PROJECT_ROOT)/obj/$(HOST_PLATFORM)
OUT_PATH         = $(BIN_ROOT)/bin2c

STB_ROOT         = $(BRANCH_ROOT)/3rdParty/stb/src

CXX_SRC_FILES    = $(wildcard $(PROJECT_ROOT)/*.cpp)
CXX_OBJ_FILES    = $(addprefix $(OBJ_ROOT)/,$(addsuffix .o, $(subst $(BRANCH_ROOT)/,,$(CXX_SRC_FILES))))

C_SRC_FILES      = $(wildcard $(STB_ROOT)/*.c)
C_OBJ_FILES      = $(addprefix $(OBJ_ROOT)/,$(addsuffix .o, $(subst $(BRANCH_ROOT)/,,$(C_SRC_FILES))))

SRC_FILES        = $(CXX_SRC_FILES) $(C_SRC_FILES)
OBJ_FILES        = $(CXX_OBJ_FILES) $(C_OBJ_FILES)

ifeq ($(HOST_OS),darwin)
	export CXX = clang++
	export CC  = clang
	DEBUGGER   = lldb
else
	export CXX = g++
	export CC  = gcc
	DEBUGGER   = gdb
endif

$(CXX_OBJ_FILES): $(CXX_SRC_FILES)
	@mkdir -p $(dir $@)
	@echo Compiling $(notdir $(patsubst %.cpp.o,%.cpp,$@))...
	@$(CXX) -g -c -o $@ $(patsubst $(OBJ_ROOT)/%.cpp.o,$(BRANCH_ROOT)/%.cpp,$@)

$(C_OBJ_FILES): $(C_SRC_FILES)
	@mkdir -p $(dir $@)
	@echo Compiling $(notdir $(patsubst %.c.o,%.c,$@))...
	@$(CC) -g -c -o $@ $(patsubst $(OBJ_ROOT)/%.c.o,$(BRANCH_ROOT)/%.c,$@)

$(OUT_PATH): $(OBJ_FILES)
	@mkdir -p $(dir $@)
	@echo Linking $@...
	@$(CXX) -g $(OBJ_FILES) -o $(OUT_PATH)

all: $(OUT_PATH)
	@echo Done!

run: $(OUT_PATH)
	@$(OUT_PATH)

run_debug: $(OUT_PATH)
	@$(DEBUGGER) $(OUT_PATH)

clean:
	@rm -rf $(BIN_ROOT) $(OBJ_ROOT)
