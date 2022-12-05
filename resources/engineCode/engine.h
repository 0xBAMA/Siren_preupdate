#ifndef ENGINE
#define ENGINE

#include "includes.h"

class engine {
public:
	engine()	{ init(); }
	~engine()	{ quit(); }

	bool mainLoop(); // called from main

private:
	// application handles + basic data
	SDL_Window * window;
	SDL_GLContext GLcontext;
	int totalScreenWidth, totalScreenHeight;
	ImVec4 clearColor;

	// renderer config
	hostParameters host;
	coreParameters core;
	lensParameters lens;
	sceneParameters scene;
	postParameters post;

	// initialization
	void init();
	void startMessage();
	void createWindowAndContext();
	void displaySetup();
	void computeShaderCompile();
	void imguiSetup();

	// main loop functions
	void mainDisplayBlit();
	void handleEvents();
	void updateNoiseOffsets();
	void pathtraceUniformUpdate();
	void clear();
	void imguiPass();
	void imguiFrameStart();
	void imguiFrameEnd();
	void drawTextEditor();
	void resetAccumulators();
	void quitConf( bool *open );
	void HelpMarker( const char* message );

	// screenshot functions
	void basicScreenShot();		// pull render target from texture memory
	void offlineScreenShot();	// render out with prescribed sample count + resolution

	// rendering functions
	void render(); 				// swichable functionality
	void postprocess();			// tonemap, dither
	glm::ivec2 getTile();		// tile renderer offset

	// shutdown procedures
	void imguiQuit();
	void SDLQuit();
	void quit();

	// program flags
	bool quitConfirm = false;
	bool pQuit = false;
	bool filter = true;

	// OpenGL data handles
		// render
	GLuint colorAccumulatorTexture;
	GLuint normalAccumulatorTexture;
	GLuint blueNoiseTexture;
	GLuint pathtraceShader;
	GLuint postprocessShader;
		// present
	GLuint displayTexture;
	GLuint displayShader;
	GLuint displayVAO;
	GLuint displayVBO;

	GLint maxTextureSizeCheck;

	// performance monitoring histories
	std::deque<float> fpsHistory;
	std::deque<float> tileHistory;
};

#endif
