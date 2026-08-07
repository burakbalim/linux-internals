CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
INCLUDES := -Iinclude

BUILD    := build
LIB_SRC  := src/util.cpp src/process.cpp src/system.cpp src/format.cpp src/mapped_file.cpp
LIB_OBJ  := $(LIB_SRC:src/%.cpp=$(BUILD)/%.o)

BIN      := $(BUILD)/procmon
DEMOS    := $(BUILD)/mmap_demo $(BUILD)/pagecache_demo

DOCKER_IMAGE := procmon-dev

.PHONY: all clean run docker-build docker-shell docker-shell-hostpid docker-run

all: $(BIN) $(DEMOS)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(LIB_OBJ) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%: demo/%.cpp $(LIB_OBJ) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(LIB_OBJ) -o $@

run: $(BIN)
	$(BIN) list

clean:
	rm -rf $(BUILD)

# --- Docker (macOS has no procfs, so development happens in a Linux container) ---

DOCKER_RUN := docker run --rm -it --name $(DOCKER_IMAGE) -v "$(CURDIR)":/work -w /work

docker-build:
	docker build -t $(DOCKER_IMAGE) .

# Mounts the source and opens an interactive shell.
# For a second shell in the SAME container (needed by mmap_demo):
#   docker exec -it $(DOCKER_IMAGE) bash
docker-shell: docker-build
	$(DOCKER_RUN) $(DOCKER_IMAGE) bash

# Same shell, but sharing the host PID namespace: every process on the VM is visible.
# Compare the two to see what PID namespace isolation actually filters.
docker-shell-hostpid: docker-build
	$(DOCKER_RUN) --pid=host $(DOCKER_IMAGE) bash

docker-run: docker-build
	$(DOCKER_RUN) $(DOCKER_IMAGE) \
		bash -c "make --no-print-directory all && $(BIN) list"
