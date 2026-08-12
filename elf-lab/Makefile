CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -g

BUILD := build

.PHONY: all clean experiments

all: $(BUILD)/greeter $(BUILD)/greeter-static $(BUILD)/libpreload.so

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/libgreet.so: src/libgreet.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -fPIC -shared $< -o $@

# Linked against libgreet, with an rpath so it is found without LD_LIBRARY_PATH.
$(BUILD)/greeter: src/greeter.cpp $(BUILD)/libgreet.so
	$(CXX) $(CXXFLAGS) $< -L$(BUILD) -lgreet -Wl,-rpath,'$$ORIGIN' -o $@

# Same program with everything baked in, for comparison.
$(BUILD)/greeter-static: src/greeter.cpp src/libgreet.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -static $^ -o $@

$(BUILD)/libpreload.so: src/libpreload.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -fPIC -shared $< -o $@

experiments: all
	./scripts/run-experiments.sh

clean:
	rm -rf $(BUILD)
