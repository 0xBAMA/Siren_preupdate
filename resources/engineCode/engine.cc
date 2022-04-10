#include "engine.h"
#include "../debug/debug.h"

// initialization of OpenGL, etc
void engine::init() {
	startMessage();
	createWindowAndContext();
	glDebugEnable();
	displaySetup();
	computeShaderCompile();
	imguiSetup();
}

// terminate ImGUI
void engine::imguiQuit() {
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

// terminate SDL2
void engine::SDLQuit() {
	SDL_GL_DeleteContext(GLcontext);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

// called from destructor
void engine::quit() {
	imguiQuit();
	SDLQuit();
}
