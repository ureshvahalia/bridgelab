.PHONY: all bidlab deallab clean clean-generated clean-all Debug Release

all: bidlab deallab

bidlab:
	$(MAKE) -C bidlab

deallab:
	$(MAKE) -C deallab

# Release builds
bidlab-release:
	$(MAKE) -C bidlab TARGET=Release

deallab-release:
	$(MAKE) -C deallab TARGET=Release

release: bidlab-release deallab-release

# Clean build artefacts in both projects (leaves shared/ generated files intact)
clean:
	$(MAKE) -C bidlab clean
	$(MAKE) -C deallab clean

# Remove flex/bison generated files from shared/ (both projects will regenerate on next build)
clean-generated:
	$(MAKE) -C bidlab clean-generated

# Full clean: build artefacts + generated files
clean-all: clean clean-generated
