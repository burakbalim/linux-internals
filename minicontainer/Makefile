CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
INCLUDES := -Iinclude

BUILD    := build
LIB_SRC  := src/util.cpp src/cgroup.cpp src/container.cpp
LIB_OBJ  := $(LIB_SRC:src/%.cpp=$(BUILD)/%.o)

BIN      := $(BUILD)/minicontainer
MEMHOG   := $(BUILD)/memhog

DOCKER_IMAGE := minicontainer-dev
# Namespaces and cgroups need real privileges; --cgroupns=private gives this container
# its own writable cgroup root instead of the host's read-only view.
DOCKER_RUN   := docker run --rm -it --name $(DOCKER_IMAGE) --privileged --cgroupns=private \
                  -v "$(CURDIR)":/work -w /work

.PHONY: all clean rootfs docker-build docker-shell

all: $(BIN) $(MEMHOG)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(LIB_OBJ) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Static so it runs inside a rootfs that has no shared libraries.
$(MEMHOG): demo/memhog.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -static $< -o $@

rootfs: $(MEMHOG)
	./scripts/make-rootfs.sh

clean:
	rm -rf $(BUILD) rootfs

docker-build:
	docker build -t $(DOCKER_IMAGE) .

docker-shell: docker-build
	$(DOCKER_RUN) $(DOCKER_IMAGE) bash
