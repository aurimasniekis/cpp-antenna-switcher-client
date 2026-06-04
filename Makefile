# Convenience wrapper around CMake/CTest workflows for antenna-switcher-client.
# The real build system is CMake — this just memorizes common invocations.

BUILD_DIR        ?= build
BUILD_TYPE       ?= Debug
JOBS             ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
GENERATOR        ?=
CMAKE            ?= cmake
CTEST            ?= ctest
LLVM_PROFDATA    ?= xcrun llvm-profdata
LLVM_COV         ?= xcrun llvm-cov

CMAKE_GEN_FLAG   := $(if $(GENERATOR),-G "$(GENERATOR)",)

.DEFAULT_GOAL := help

.PHONY: help
help:
	@echo "antenna-switcher-client — make targets"
	@echo ""
	@echo "  make configure         Configure $(BUILD_DIR)/ ($(BUILD_TYPE))"
	@echo "  make build             Build everything in $(BUILD_DIR)/"
	@echo "  make test              Run ctest in $(BUILD_DIR)/"
	@echo "  make examples          Build the example programs"
	@echo "  make cli               Build the antenna-switcher-cli tool"
	@echo "  make all               configure + build + test"
	@echo ""
	@echo "  make sanitize          Configure+build+test in build-san/ with ASan+UBSan"
	@echo "  make tidy              Configure+build in build-tidy/ with clang-tidy"
	@echo "  make tidy-fix          Like tidy, but apply clang-tidy fixes in place"
	@echo "  make release           Configure+build+test in build-release/ (Release)"
	@echo "  make coverage          Configure+build+test in build-coverage/ with Clang coverage"
	@echo "  make docs              Configure+build Doxygen HTML in build-docs/"
	@echo ""
	@echo "  make format            Run clang-format -i over project sources"
	@echo "  make format-check      Verify formatting without writing"
	@echo ""
	@echo "  make ci                Pre-push gate: format-check + tidy + test + sanitize + release"
	@echo ""
	@echo "  make clean             Remove $(BUILD_DIR)/"
	@echo "  make distclean         Remove all build-* directories"
	@echo ""
	@echo "Variables: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS)"

.PHONY: configure
configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

.PHONY: build
build: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

.PHONY: test
test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

.PHONY: examples
examples: configure
	$(CMAKE) --build $(BUILD_DIR) --target control -j $(JOBS)

.PHONY: cli
cli: configure
	$(CMAKE) --build $(BUILD_DIR) --target antenna-switcher-cli -j $(JOBS)

.PHONY: all
all: test

.PHONY: ci
ci: format-check tidy test sanitize release
	@echo ""
	@echo "ci: all checks passed"

.PHONY: sanitize
sanitize:
	$(CMAKE) -S . -B build-san $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DANTENNA_SWITCHER_ENABLE_SANITIZERS=ON
	$(CMAKE) --build build-san -j $(JOBS)
	$(CTEST) --test-dir build-san --output-on-failure

.PHONY: tidy
tidy:
	$(CMAKE) -S . -B build-tidy $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DANTENNA_SWITCHER_ENABLE_CLANG_TIDY=ON
	$(CMAKE) --build build-tidy -j $(JOBS)

.PHONY: tidy-fix
tidy-fix:
	$(CMAKE) -S . -B build-tidy $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DANTENNA_SWITCHER_ENABLE_CLANG_TIDY=ON
	$(CMAKE) --build build-tidy -j $(JOBS) -- CXX_CLANG_TIDY_EXTRA_ARGS=--fix || true
	@echo "tidy-fix: applied available fixes"

.PHONY: release
release:
	$(CMAKE) -S . -B build-release $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build build-release -j $(JOBS)
	$(CTEST) --test-dir build-release --output-on-failure

.PHONY: coverage
coverage:
	$(CMAKE) -S . -B build-coverage $(CMAKE_GEN_FLAG) \
		-DCMAKE_BUILD_TYPE=Debug -DANTENNA_SWITCHER_ENABLE_COVERAGE=ON
	$(CMAKE) --build build-coverage -j $(JOBS)
	rm -f build-coverage/*.profraw build-coverage/antenna-switcher.profdata
	LLVM_PROFILE_FILE="$(CURDIR)/build-coverage/antenna-switcher-%p.profraw" \
		$(CTEST) --test-dir build-coverage --output-on-failure
	$(LLVM_PROFDATA) merge -sparse build-coverage/*.profraw \
		-o build-coverage/antenna-switcher.profdata
	$(LLVM_COV) report build-coverage/tests/antenna_switcher_tests \
		-instr-profile=build-coverage/antenna-switcher.profdata \
		-ignore-filename-regex='(_deps|tests|generated)/'
	$(LLVM_COV) show build-coverage/tests/antenna_switcher_tests \
		-instr-profile=build-coverage/antenna-switcher.profdata \
		-ignore-filename-regex='(_deps|tests|generated)/' \
		-format=html -output-dir=build-coverage/coverage-html \
		-show-line-counts-or-regions
	@echo "HTML report: build-coverage/coverage-html/index.html"

.PHONY: docs
docs:
	$(CMAKE) -S . -B build-docs $(CMAKE_GEN_FLAG) \
	    -DANTENNA_SWITCHER_BUILD_DOCS=ON \
	    -DANTENNA_SWITCHER_BUILD_TESTS=OFF \
	    -DANTENNA_SWITCHER_BUILD_EXAMPLES=OFF
	$(CMAKE) --build build-docs --target antenna_switcher_docs -j $(JOBS)
	@echo "HTML report: build-docs/docs/html/index.html"

FORMAT_FILES := $(shell find include tests examples src bin -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' \) 2>/dev/null)

.PHONY: format
format:
	@if [ -z "$(FORMAT_FILES)" ]; then echo "no source files found"; else clang-format -i $(FORMAT_FILES); fi

.PHONY: format-check
format-check:
	@if [ -z "$(FORMAT_FILES)" ]; then echo "no source files found"; else clang-format --dry-run --Werror $(FORMAT_FILES); fi

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean:
	rm -rf build build-san build-tidy build-release build-coverage build-docs cmake-build-*
