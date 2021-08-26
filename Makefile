V8_VERSION=0.1.0

default_target: help

help:

help: ## this text
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z0-9_\.-]+:.*?## / {printf "\033[36m%-10s - \033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	

cmake: ## set up the build folder
	mkdir -p build
	cmake -S . -B ./build

cbuild: ## build binary
	cmake --build build

clean: ## remove build artifacts
	rm -rf build
	mkdir -p build

all: cmake cbuild ## cmake & cbuild