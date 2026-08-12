CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g -pthread
INCLUDES := -Iinclude

BUILD    := build
LIB_SRC  := src/socket.cpp src/signals.cpp src/server.cpp src/server_epoll.cpp src/server_threads.cpp src/server_uring.cpp
LIB_OBJ  := $(LIB_SRC:src/%.cpp=$(BUILD)/%.o)

BIN      := $(BUILD)/evserver
LOADGEN  := $(BUILD)/loadgen

DOCKER_IMAGE := evserver-dev
DOCKER_RUN   := docker run --rm -it --name $(DOCKER_IMAGE) -v "$(CURDIR)":/work -w /work

.PHONY: all clean docker-build docker-shell docker-shell-trace

all: $(BIN) $(LOADGEN)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(LIB_OBJ) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -luring -o $@

$(LOADGEN): bench/loadgen.cpp $(BUILD)/socket.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(BUILD)/socket.o -o $@

clean:
	rm -rf $(BUILD)

docker-build:
	docker build -t $(DOCKER_IMAGE) .

# Opens a shell. For a second shell in the same container:
#   docker exec -it evserver-dev bash
docker-shell: docker-build
	$(DOCKER_RUN) $(DOCKER_IMAGE) bash

# strace and perf need extra capabilities that a default container does not have.
docker-shell-trace: docker-build
	$(DOCKER_RUN) --cap-add=SYS_PTRACE --cap-add=SYS_ADMIN \
		--security-opt seccomp=unconfined $(DOCKER_IMAGE) bash
