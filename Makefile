CC        := g++
LD        := g++
CC_FLAGS  := -std=c++17 -O3 -g

MODULES    := exec host nvm_chip nvm_chip/flash_memory sim ssd utils
SRC_DIR    := $(addprefix src/,$(MODULES)) src
BUILD_DIR  := $(addprefix build/,$(MODULES)) build

SRC        := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.cpp))
SRC        := src/main.cpp $(SRC)
OBJ        := $(patsubst src/%.cpp,build/%.o,$(SRC))
INCLUDES   := $(addprefix -I,$(SRC_DIR))

ifeq ($(OS),Windows_NT)
	EXECUTABLE := MQSim.exe
else
	EXECUTABLE := MQSim
endif

ifeq ($(OS),Windows_NT)
	MKDIR_P = if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
	RM_DIR  = for %%D in ($(subst /,\,$(BUILD_DIR))) do if exist %%D rmdir /s /q %%D
	RM_EXE  = if exist $(EXECUTABLE) del /q $(EXECUTABLE)
else
	MKDIR_P = mkdir -p $@
	RM_DIR  = rm -rf $(BUILD_DIR)
	RM_EXE  = rm -f $(EXECUTABLE)
endif

vpath %.cpp $(SRC_DIR)

define make-goal
$1/%.o: %.cpp
	$(CC) $(CC_FLAGS) $(INCLUDES) -c $$< -o $$@
endef
$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))

.PHONY: all checkdirs clean

all: checkdirs $(EXECUTABLE)

$(EXECUTABLE): $(OBJ)
	$(LD) $^ -o $@

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	$(MKDIR_P)

clean:
	$(RM_DIR)
	$(RM_EXE)

