# Convenience targets for local development.
# On Windows, run with `mingw32-make dev` if `make` is not available.

BUILD_DIR ?= build
JOBS ?= 4

ifeq ($(OS),Windows_NT)
EXE := $(BUILD_DIR)/myterm.exe
CMAKE_GENERATOR ?= MinGW Makefiles
DEFAULT_GCC := C:/ProgramData/mingw64/mingw64/bin/gcc.exe
ifeq ($(strip $(CMAKE_C_COMPILER)),)
ifneq ($(wildcard $(DEFAULT_GCC)),)
CMAKE_C_COMPILER := $(DEFAULT_GCC)
endif
endif
RUN_CMD := powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev.ps1 -ExePath "$(EXE)"
SMOKE_CMD := powershell.exe -ExecutionPolicy Bypass -File tests/smoke_test.ps1 -ExePath $(EXE)
else
EXE := $(BUILD_DIR)/myterm
CMAKE_GENERATOR ?= Ninja
RUN_CMD := $(EXE)
SMOKE_CMD := echo "Smoke test is only implemented for Windows right now."
endif

CONFIGURE_ARGS := -B $(BUILD_DIR) -G "$(CMAKE_GENERATOR)"
ifneq ($(strip $(CMAKE_C_COMPILER)),)
CONFIGURE_ARGS += -DCMAKE_C_COMPILER=$(CMAKE_C_COMPILER)
endif

.PHONY: configure build dev run test smoke clean rebuild help

help:
	@echo "Targets:"
	@echo "  make dev    - configure, build, and run myterm"
	@echo "  make run    - run the latest built myterm"
	@echo "  make build  - configure and build"
	@echo "  make test   - run CTest"
	@echo "  make smoke  - run the Windows smoke test"
	@echo "  make clean  - clean the build directory"
	@echo "  make rebuild- clean, build, and run"

configure:
	cmake $(CONFIGURE_ARGS)

build: configure
	cmake --build $(BUILD_DIR) -j $(JOBS)

run:
	$(RUN_CMD)

dev: build run

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

smoke: build
	$(SMOKE_CMD)

clean:
	@if [ -d "$(BUILD_DIR)" ]; then cmake --build $(BUILD_DIR) --target clean; fi

rebuild: clean dev
