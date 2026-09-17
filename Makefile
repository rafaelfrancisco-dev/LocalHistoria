CMAKE_PREFIX_PATH ?= $(shell brew --prefix 2>/dev/null)
JOBS ?= 4
GEO_PYTHON ?= .venv-geography/bin/python

.PHONY: all test integration run data clean
all: data
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(CMAKE_PREFIX_PATH)"
	cmake --build build -j $(JOBS)
data: data/world.sqlite web/world.geojson
data/world.sqlite web/world.geojson: tools/import_data.py data/source/countries.geojson data/source/json-cities.json.gz
	$(GEO_PYTHON) -c "import shapely"
	python3 tools/import_data.py
	$(GEO_PYTHON) tools/import_regions.py
test: all
	ctest --test-dir build --output-on-failure
integration: all
	python3 tests/integration.py
run: all
	./build/localhistoria
clean:
	rm -rf build

geography-env:
	python3 -m venv .venv-geography
	.venv-geography/bin/pip install -r tools/requirements-geography.txt

geography-test:
	$(GEO_PYTHON) tests/regions_test.py
