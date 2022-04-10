# Built on not-quite-a-demo-engine (aka NQADE /eŋkweɪd/)

# Setup
- Requires `libsdl2-dev` on Ubuntu.
- Make sure to recurse submodules to pull in FastNoise2 code - `git submodule update --init --recursive`, alternatively just run init.sh to do the same thing.
- Run build.sh in order to build on Ubuntu. It just automates the building of the executable and moves it to the root directory, as files are referenced relative to that location, and then deletes the build folder (if called with the "clean" option).



## Features
**Graphics Stuff**
- [SDL2](https://wiki.libsdl.org/) is used for windowing and input handling
- [GL3W](https://github.com/skaslev/gl3w) as an OpenGL loader
- OpenGL Debug callback for error/warning reporting
- [Dear ImGui Docking Branch](https://github.com/ocornut/imgui/tree/docking) + [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit)
	- Setup to spawn platform windows


**Noise**
- Diamond-Square algorithm heightmap generation
- [FastNoise2](https://github.com/Auburn/FastNoise2) - flexible, powerful, very fast noise generation
- 4 channel blue noise texture from [Christoph Peters](http://momentsingraphics.de/BlueNoise.html)


**Utilities**
- CMake build setup
- [GLM](http://glm.g-truc.net/0.9.8/api/index.html) for vector and matrix types
- [LodePNG](https://lodev.org/lodepng/) for loading/saving of PNG images ( supports transparency and very large images )
- JSON parsing using [nlohmann's single header implementation](https://github.com/nlohmann/json)
- TinyOBJLoader for loading of Wavefront .OBJ 3D model files
- [Brent Werness' Voxel Automata Terrain ( VAT )](https://bitbucket.org/BWerness/voxel-automata-terrain/src/master/), converted from processing to C++
	- BigInt library required by the VAT implementation ( to replace java/processing BigInt )


	Rewrite of SDF_Path using NQADE

	Some areas to improve / mess around with:
	- Timing on tile rendering loop, to keep things responsive - this wasn't working on the last implementation, needs work - probably use OpenGL timing queries instead of std::chrono, that might be all I need to do
	- Finish implementing dither logic - port [this color space](https://bottosson.github.io/posts/colorpicker/) and see if there's any other interesting ones
	- The glitch logic that I was thinking about before:
		- Pull image data to CPU or do in a shader and write back to the accumulator texture
			- Add to it by glyph stamps
			- Mask for noise application ( + / - )
			- Blurs
			- Clears ( every other row / checkerboard )
			- Dither inside tile
			- Reset averaging ( sample count )
			- Use std::vector + shuffle to on an array of floats cast to a byte array - make a mess of the data
