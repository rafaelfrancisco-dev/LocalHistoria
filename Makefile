CMAKE_PREFIX_PATH ?= $(shell brew --prefix 2>/dev/null)
JOBS ?= 4
GEO_PYTHON ?= .venv-geography/bin/python
CXX_SOURCES := $(shell find src include tests tools -name '*.cpp' -o -name '*.h')

.PHONY: all test integration run start data display clean format format-check
all: data
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(CMAKE_PREFIX_PATH)"
	cmake --build build -j $(JOBS)
data: data/world.sqlite web/regions.topojson data/.stats-imported
data/world.sqlite: tools/import_data.py tools/import_regions.py tools/region_coastal.py data/source/countries.geojson data/source/json-cities.json.gz
	$(GEO_PYTHON) -c "import shapely"
	python3 tools/import_data.py
	$(GEO_PYTHON) tools/import_regions.py
	python3 tools/import_stats.py
	touch data/.stats-imported
# National statistics need only Python: refreshing them never rebuilds geography.
data/.stats-imported: tools/import_stats.py data/source/country_stats.json data/source/currencies.json data/source/stats_overrides.json data/source/world_2026.json data/source/military_2026.json
	python3 tools/import_stats.py
	touch $@
test: all
	ctest --test-dir build --output-on-failure
integration: all
	python3 tests/integration.py
run: all
	./build/localhistoria
start:
	./start.sh
clean:
	rm -rf build
format:
	clang-format -i $(CXX_SOURCES)
format-check:
	clang-format --dry-run --Werror $(CXX_SOURCES)

geography-env:
	python3 -m venv .venv-geography
	.venv-geography/bin/pip install -r tools/requirements-geography.txt

# Browser display geometry (simplified TopoJSON regions, GeoJSON countries) from the existing SQLite.
# import_regions.py also runs it; this rebuilds only the display files.
web/regions.topojson web/world.geojson: tools/display_geometry.py
	$(GEO_PYTHON) tools/display_geometry.py
display:
	$(GEO_PYTHON) tools/display_geometry.py

geography-test:
	$(GEO_PYTHON) tests/regions_test.py
