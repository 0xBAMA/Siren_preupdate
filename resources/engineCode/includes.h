#ifndef INCLUDES
#define INCLUDES

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// stl includes
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// iostream stuff
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::flush;

// pi definition - definitely sufficient precision
constexpr double pi = 3.14159265358979323846;

// MSAA count - effects OpenGL geometry evaluation
constexpr int MSAACount = 1;

// vector math library GLM
#define GLM_FORCE_SWIZZLE
#define GLM_SWIZZLE_XYZW
#include "../GLM/glm.hpp"                  //general vector types
#include "../GLM/gtc/matrix_transform.hpp" // for glm::ortho
#include "../GLM/gtc/type_ptr.hpp"         //to send matricies gpu-side
#include "../GLM/gtx/rotate_vector.hpp"
#include "../GLM/gtx/transform.hpp"
#include "../GLM/gtx/quaternion.hpp"
#include "../GLM/gtx/string_cast.hpp"

// not sure as to the utility of this
#define GLX_GLEXT_PROTOTYPES

// OpenGL Loader
#include "../ImGUI/gl3w.h"

// GUI library (dear ImGUI)
#include "../ImGUI/TextEditor.h"
#include "../ImGUI/imgui.h"
#include "../ImGUI/imgui_impl_sdl.h"
#include "../ImGUI/imgui_impl_opengl3.h"

// SDL includes - windowing, gl context, system info
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

// png loading library - very powerful
#include "../LodePNG/lodepng.h"

// wrapper for TinyOBJLoader
#include "../TinyOBJLoader/objLoader.h"

// shader compilation wrapper
#include "shaders/shader.h"

// coloring of CLI output
#include "../fonts/colors.h"

// diamond square heightmap generation
#include "../noise/diamondSquare/diamond_square.h"

// Brent Werness' Voxel Automata Terrain
#include "../noise/VAT/VAT.h"

// more general noise solution
#include "../noise/FastNoise2/include/FastNoise/FastNoise.h"

// Niels Lohmann - JSON for Modern C++
#include "../JSON/json.hpp"
using json = nlohmann::json;

// #define WIDTH 640
// #define HEIGHT 480

#define WIDTH  1920 / 3
#define HEIGHT 1080 / 3

#define PERFORMANCEHISTORY 250

// not sure about this, going to need to do some testing, to maximize amount of work per timer query
#define TILESIZE 16

struct hostParameters {
	int screenshotDim = WIDTH; // maintain ratio with HEIGHT/WIDTH
	int tilePerFrameCap = 128;	// max number of tiles to be executed in a frame update
	int numSamplesScreenshot = 128; // how many samples to take when rendering the screenshot
};

struct coreParameters {
	glm::ivec2 tileOffset = glm::ivec2( 0, 0 ); // x, y of current tile
	// updated once a frame, offset blue noise texture so there isn't Voraldo's stroke pattern in v1.2
	glm::ivec2 noiseOffset = glm::ivec2( 0, 0 );
	int maxSteps = 100;
	int maxBounces = 10;
	float maxDistance = 15.0;
	float epsilon = 0.001;
	float exposure = 0.98;
	float focusDistance = 10.0;
	int normalMethod = 1;
	float FoV = 0.152;
	glm::vec3 viewerPosition = glm::vec3( 10.0, 9.258, 7.562 );
	float rotationAboutX = 1.541;
	float rotationAboutY = 1.117;
	float rotationAboutZ = 1.654;
	glm::vec3 basisX;
	glm::vec3 basisY;
	glm::vec3 basisZ;
};

struct lensParameters {
	float lensScaleFactor = 1.0;
	float lensRadius1 = 10.0;
	float lensRadius2 = 3.0;
	float lensThickness = 0.3;
	float lensRotate = 0.0;
	float lensIOR = 1.2;
	int lensNormalMethod = 0;
};

struct postParameters {
	int ditherMode = 0;
	int ditherMethod = 0;
	int ditherPattern = 0;
	int tonemapMode = 0;
	int depthMode = 0;
	float depthScale = 0.0;
};
#endif
