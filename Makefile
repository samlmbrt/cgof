.PHONY: configure debug release clean format

configure:
	cmake --preset debug
	cmake --preset release

debug:
	@[ -d build/debug ] || cmake --preset debug
	cmake --build --preset debug && ./build/debug/cgof

release:
	@[ -d build/release ] || cmake --preset release
	cmake --build --preset release && ./build/release/cgof

format:
	find src \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format -i

clean:
	rm -rf build
