# Makefile for DJI Tello Drone Controller

# Default cargo binary
CARGO := cargo
# Directory for generated documentation
DOC_DIR := target/doc
# Release binary path
RELEASE_BIN := target/release/dji_tello_lib
# Debug binary path
DEBUG_BIN := target/debug/dji_tello_lib

# Default target
.PHONY: all
all: build

# Build the project in debug mode
.PHONY: build
build:
	@echo "Building project..."
	$(CARGO) build
	@echo "Build completed."

# Build the project in release mode
.PHONY: release
release:
	@echo "Building project in release mode..."
	$(CARGO) build --release
	@echo "Release build completed."

# Clean the project
.PHONY: clean
clean:
	@echo "Cleaning project..."
	$(CARGO) clean
	@echo "Clean completed."

# Run the project in debug mode
.PHONY: run
run: build
	@echo "Running application..."
	$(CARGO) run

# Run the project in release mode
.PHONY: run-release
run-release: release
	@echo "Running application in release mode..."
	$(RELEASE_BIN)

# Run the unit tests
.PHONY: test
test:
	@echo "Running tests..."
	$(CARGO) test
	@echo "Tests completed."

# Run the tests with verbose output
.PHONY: test-verbose
test-verbose:
	@echo "Running tests with verbose output..."
	$(CARGO) test -- --nocapture
	@echo "Tests completed."

# Generate documentation
.PHONY: doc
doc:
	@echo "Generating documentation..."
	$(CARGO) doc --no-deps
	@echo "Documentation generated in $(DOC_DIR)"

# Open the generated documentation in the default browser
.PHONY: doc-open
doc-open: doc
	@echo "Opening documentation in browser..."
	$(CARGO) doc --no-deps --open

# Check code formatting
.PHONY: fmt-check
fmt-check:
	@echo "Checking code formatting..."
	$(CARGO) fmt --all -- --check

# Format the code
.PHONY: fmt
fmt:
	@echo "Formatting code..."
	$(CARGO) fmt --all
	@echo "Code formatting completed."

# Run linter
.PHONY: lint
lint:
	@echo "Running linter..."
	$(CARGO) clippy -- -D warnings
	@echo "Linting completed."

# Run benchmark tests if available
.PHONY: bench
bench:
	@echo "Running benchmarks..."
	$(CARGO) bench
	@echo "Benchmarks completed."

# Create a new release
.PHONY: tag-release
tag-release:
	@echo "Creating git tag for release..."
	git tag -a v$$(grep -m1 "version" Cargo.toml | cut -d '"' -f2) -m "Release v$$(grep -m1 "version" Cargo.toml | cut -d '"' -f2)"
	@echo "Tag created. Run 'git push --tags' to push to remote repository."

# Show help information
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all          - Build the project (default)"
	@echo "  build        - Build the project in debug mode"
	@echo "  release      - Build the project in release mode"
	@echo "  clean        - Clean the project"
	@echo "  run          - Run the project in debug mode"
	@echo "  run-release  - Run the project in release mode"
	@echo "  test         - Run the unit tests"
	@echo "  test-verbose - Run the tests with verbose output"
	@echo "  doc          - Generate documentation"
	@echo "  doc-open     - Generate and open documentation in browser"
	@echo "  fmt-check    - Check code formatting"
	@echo "  fmt          - Format the code"
	@echo "  lint         - Run linter (clippy)"
	@echo "  bench        - Run benchmark tests if available"
	@echo "  tag-release  - Create git tag for current version"
	@echo "  help         - Show this help information"