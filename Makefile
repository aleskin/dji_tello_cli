# Thin wrapper over CMake. Override the cmake binary with `make CMAKE=/path/to/cmake`.
CMAKE     ?= cmake
CTEST     ?= $(patsubst %cmake,%ctest,$(CMAKE))
BUILD_DIR ?= build
JOBS      ?= $(shell nproc 2>/dev/null || echo 4)
SIM_PORT  ?= 8889

.PHONY: all build test sim run-sim clean bump-dry bump-version sanity-check format

all: build

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

# Fake drone on 127.0.0.1:$(SIM_PORT).
sim: build
	./$(BUILD_DIR)/tello-sim --port $(SIM_PORT)

# CLI against a simulator started in the background; the simulator stops with the CLI.
run-sim: build
	@./$(BUILD_DIR)/tello-sim --port $(SIM_PORT) & SIM=$$!; \
	trap 'kill $$SIM 2>/dev/null' EXIT INT TERM; \
	./$(BUILD_DIR)/tello --host 127.0.0.1 --port $(SIM_PORT)

clean:
	rm -rf $(BUILD_DIR)

# Version bump: Makefile.version + CHANGELOG.md + TODO.md, one commit. TYPE=patch|minor|major.
bump-dry:
	python3 scripts/bump.py $(or $(TYPE),patch) --dry-run

bump-version:
	python3 scripts/bump.py $(or $(TYPE),patch)

# Static analysis report (clang-tidy + cppcheck) → logs/sanity/last.txt.
sanity-check: build
	bash scripts/sanity_check.sh

# Code style (.clang-format: Allman, 4 spaces). Override the binary with CLANG_FORMAT=...
CLANG_FORMAT ?= clang-format
format:
	$(CLANG_FORMAT) -i $$(git ls-files '*.cpp' '*.hpp')
