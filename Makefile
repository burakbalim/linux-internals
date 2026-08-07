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

# --- Docker (macOS'ta /proc olmadigi icin gelistirme burada yapilir) ---

docker-build:
	docker build -t $(DOCKER_IMAGE) .

# Kaynagi mount edip interaktif kabuk acar.
docker-shell: docker-build
	docker run --rm -it -v "$(CURDIR)":/work -w /work $(DOCKER_IMAGE) bash

# Ayni kabuk, ama PID namespace paylasimli: container yerine VM'in tum islemleri gorunur.
# Compare the two to see what PID namespace isolation actually filters.
docker-shell-hostpid: docker-build
	docker run --rm -it --pid=host -v "$(CURDIR)":/work -w /work $(DOCKER_IMAGE) bash

docker-run: docker-build
	docker run --rm -it -v "$(CURDIR)":/work -w /work $(DOCKER_IMAGE) \
		bash -c "make --no-print-directory all && $(BIN) list"
