all:
	DOCKER_BUILDKIT=1 docker build -t lyuba .
	mkdir -p artefacts
	docker run -v ${PWD}:/data lyuba make release

release:
	platformio run
	mkdir -p /data/artefacts
	cp .pio/build/esp32doit-devkit-v1/*.map .pio/build/esp32doit-devkit-v1/*.bin .pio/build/esp32doit-devkit-v1/*.elf /data/artefacts/

