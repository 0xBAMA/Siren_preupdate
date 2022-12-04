#include "engine.h"

bool engine::mainLoop() {
	render();											// render with the current mode
	postprocess( );								// colorAccumulatorTexture -> displayTexture
	mainDisplayBlit();						// fullscreen triangle copying the image
	imguiPass();									// do all the GUI stuff
	SDL_GL_SwapWindow( window );	// swap the double buffers to present
	handleEvents();								// handle input events
	return !pQuit;								// break loop in main.cc when pQuit becomes true
}

void engine::render() {
	// different rendering modes - preview until pathtrace is triggered
	switch ( mode ) {
		case renderMode::preview:   raymarch();  break;
		case renderMode::pathtrace: pathtrace(); break;
		default: break;
	}
}

void engine::raymarch() {
	glUseProgram( raymarchShader );
	// do a fullscreen pass with simple shading, flag to prevent unncessesary further update

	// send associated uniforms

	// not sure if I actually want to include this as a feature, but that may be laziness talking
		// will revisit once the pathtracing logic is implemented
}

void engine::updateNoiseOffsets () {
	std::random_device r;
	std::seed_seq s{ r(), r(), r(), r(), r(), r(), r(), r(), r() };
	auto gen = std::mt19937_64( s );
	std::uniform_int_distribution< int > dist( 0, 512 );
	core.noiseOffset.x = dist( gen );
	core.noiseOffset.y = dist( gen );
}

void engine::pathtraceUniformUpdate() {

	glUniform2i( glGetUniformLocation( pathtraceShader, "noiseOffset" ), core.noiseOffset.x, core.noiseOffset.y );
	glUniform1i( glGetUniformLocation( pathtraceShader, "maxSteps" ), core.maxSteps );
	glUniform1i( glGetUniformLocation( pathtraceShader, "maxBounces" ), core.maxBounces );
	glUniform1f( glGetUniformLocation( pathtraceShader, "maxDistance" ), core.maxDistance );
	glUniform1f( glGetUniformLocation( pathtraceShader, "epsilon" ), core.epsilon );
	glUniform1i( glGetUniformLocation( pathtraceShader, "normalMethod" ), core.normalMethod );
	glUniform1f( glGetUniformLocation( pathtraceShader, "focusDistance" ), core.focusDistance );
	glUniform1f( glGetUniformLocation( pathtraceShader, "FoV" ), core.FoV );
	glUniform1f( glGetUniformLocation( pathtraceShader, "exposure" ), core.exposure );
	glUniform3f( glGetUniformLocation( pathtraceShader, "viewerPosition" ), core.viewerPosition.x, core.viewerPosition.y, core.viewerPosition.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "basisX"), core.basisX.x, core.basisX.y, core.basisX.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "basisY"), core.basisY.x, core.basisY.y, core.basisY.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "basisZ"), core.basisZ.x, core.basisZ.y, core.basisZ.z );
	glUniform1f( glGetUniformLocation( pathtraceShader, "understep" ), core.understep );

	glUniform1f( glGetUniformLocation( pathtraceShader, "lensScaleFactor" ), lens.lensScaleFactor );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensRadius1" ), lens.lensRadius1 );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensRadius2" ), lens.lensRadius2 );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensThickness" ), lens.lensThickness );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensRotate" ), lens.lensRotate );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensIOR" ), lens.lensIOR );

	glUniform3f( glGetUniformLocation( pathtraceShader, "redWallColor" ), scene.redWallColor.x, scene.redWallColor.y, scene.redWallColor.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "greenWallColor" ), scene.greenWallColor.x, scene.greenWallColor.y, scene.greenWallColor.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "metallicDiffuse" ), scene.metallicDiffuse.x, scene.metallicDiffuse.y, scene.metallicDiffuse.z );
}

void engine::pathtrace() {
	glUseProgram( pathtraceShader );

	//
	updateNoiseOffsets();

	// send the uniforms
	pathtraceUniformUpdate();

	GLuint64 startTime, checkTime;
	GLuint queryID[ 2 ];
	glGenQueries( 2, queryID );
	glQueryCounter( queryID[ 0 ], GL_TIMESTAMP );

	// get startTime
	GLint startTimeAvailable = 0;
	while( !startTimeAvailable )
	glGetQueryObjectiv( queryID[ 0 ], GL_QUERY_RESULT_AVAILABLE, &startTimeAvailable );
	glGetQueryObjectui64v( queryID[ 0 ], GL_QUERY_RESULT, &startTime );

	// used for the wang hash seeding, as well as the noise offset
	static std::default_random_engine gen;
	static std::uniform_int_distribution< int > dist( 0, std::numeric_limits< int >::max() / 4 );

	int tilesCompleted = 0;
	float looptime = 0.;
	while( 1 ) {
		// get a tile offset + send it
		glm::ivec2 tile = getTile();
		glUniform2i( glGetUniformLocation( pathtraceShader, "tileOffset" ), tile.x, tile.y );

		// seeding the wang rng in the shader - shader uses both the screen location and this value
		int value = dist( gen );
		// cout << value << endl;
		glUniform1i( glGetUniformLocation( pathtraceShader, "wangSeed" ), value );

		// render the specified tile - dispatch
		glDispatchCompute( TILESIZE / 16, TILESIZE / 16, 1 );
		// glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
		glMemoryBarrier(  GL_ALL_BARRIER_BITS );
		tilesCompleted++;

		// check time, wait for query to be ready
		glQueryCounter( queryID[ 1 ], GL_TIMESTAMP );
		// glMemoryBarrier( GL_QUERY_BUFFER_BARRIER_BIT );
		GLint checkTimeAvailable = 0;
		while( !checkTimeAvailable )
			glGetQueryObjectiv( queryID[ 1 ], GL_QUERY_RESULT_AVAILABLE, &checkTimeAvailable );
		glGetQueryObjectui64v( queryID[ 1 ], GL_QUERY_RESULT, &checkTime );

		// break if duration exceeds 16 ms ( 60fps + a small margin ) - query units are nanoseconds
		looptime = ( checkTime - startTime ) / 1e6; // get milliseconds
		if( looptime > ( 16.0f ) || tilesCompleted >= host.tilePerFrameCap ) {
			// cout << tilesCompleted << " tiles in " << looptime << " ms, avg " << looptime / tilesCompleted << " ms/tile" << endl;
			break;
		}
	}
	fpsHistory.push_back( 1000.0f / looptime );
	fpsHistory.pop_front();

	tileHistory.push_back( tilesCompleted );
	tileHistory.pop_front();
}

void engine::postprocess() {
	// tonemapping and dithering, as configured in the GUI
	glUseProgram( postprocessShader );

	// send associated uniforms

	glDispatchCompute( std::ceil( WIDTH / 32. ), std::ceil( HEIGHT / 32. ), 1 );
	glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT ); // sync
}

void engine::mainDisplayBlit() {
	// clear the screen
	glClearColor( clearColor.x, clearColor.y, clearColor.z, clearColor.w );
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	// texture display
	glUseProgram( displayShader );
	glBindVertexArray( displayVAO );

	ImGuiIO &io = ImGui::GetIO();
	glUniform2f( glGetUniformLocation( displayShader, "resolution" ), io.DisplaySize.x, io.DisplaySize.y );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

void engine::resetAccumulators() {
	std::vector< unsigned char > imageData;
	imageData.resize( WIDTH * HEIGHT * 4, 0 );
	// reset color accumulator
	glActiveTexture( GL_TEXTURE0 + 1 );
	glBindTexture( GL_TEXTURE_2D, colorAccumulatorTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, &imageData[ 0 ] );
	// reset normal/depth accumulator
	glActiveTexture( GL_TEXTURE0 + 2 );
	glBindTexture( GL_TEXTURE_2D, normalAccumulatorTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, &imageData[ 0 ] );
	// wait for sync
	glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
	fullscreenPasses = 0; // reset sample count
	cout << "Accumulator Buffer has been reinitialized" << endl;
}

void engine::imguiPass() {
	// start the imgui frame
	imguiFrameStart();

	// show the demo window
	static bool showDemoWindow = !true;
	if ( showDemoWindow ) {
		ImGui::ShowDemoWindow( &showDemoWindow );
	}

	// show quit confirm window
	quitConf( &quitConfirm );

	// Tabbed window for the controls categories of parameters
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar;
	ImGui::Begin( "Settings", NULL, flags );

	ImGui::Text(" Parameters ");
	if ( ImGui::BeginTabBar( "Config Sections", ImGuiTabBarFlags_None ) ) {
		if ( ImGui::BeginTabItem( " Host " ) ) {
			ImGui::SliderInt( "Screenshot Width", &host.screenshotDim, 1000, maxTextureSizeCheck );
			ImGui::SliderInt( "Screenshot Samples", &host.numSamplesScreenshot, 1, 512 );
			ImGui::Separator();
			ImGui::SliderInt( "Tile Per Frame Cap", &host.tilePerFrameCap, 1, 3000 );

			if( ImGui::SmallButton( "Framebuffer Screenshot" ) ) {
				basicScreenShot();
			}

			// what else?
			// buttons, controls for the renderer state
				// trigger random tile glitch behaviors

			if ( ImGui::SmallButton( "Reset Buffer Samples" ) ) {
				resetAccumulators(); // also triggered by 'r'
			}

			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( " Core " ) ) {
			// core renderer parameters
			ImGui::SliderInt( "Max Raymarch Steps", &core.maxSteps, 1, 500 );
			ImGui::SliderInt( "Max Light Bounces", &core.maxBounces, 1, 50 );
			ImGui::SliderFloat( "Max Raymarch Distance", &core.maxDistance, 0.0, 200.0 );
			ImGui::SliderFloat( "Raymarch Understep", &core.understep, 0.1, 1.0 );
			ImGui::SliderFloat( "Raymarch Epsilon", &core.epsilon, 0.0001, 0.1, "%.4f" );
			ImGui::Separator();
			ImGui::SliderFloat( "Exposure", &core.exposure, 0.1, 3.6 );
			// ImGui::SliderFloat( "Thin Lens Focus Distance", &core.focusDistance, 0.0, 200.0 );
			// ImGui::SliderFloat( "Thin Lens Effect Intensity", &core.thinLensIntensity, 0.0, 5.0 );
			ImGui::Separator();
			ImGui::SliderInt( "SDF Normal Method", &core.normalMethod, 1, 3 );
			ImGui::SliderFloat( "Field of View", &core.FoV, 0.01, 2.5 );
			ImGui::Separator();
			ImGui::SliderFloat( "Viewer X", &core.viewerPosition.x, -20.0, 20.0 );
			ImGui::SliderFloat( "Viewer Y", &core.viewerPosition.y, -20.0, 20.0 );
			ImGui::SliderFloat( "Viewer Z", &core.viewerPosition.z, -20.0, 20.0 );

			// have it tell what the current set of basis vectors is

			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( " Lens / Model " ) ) {
			// lens geometry parameters
			ImGui::SliderFloat( "Lens Scale Factor", &lens.lensScaleFactor, 0.001, 2.5 );
			ImGui::SliderFloat( "Lens Radius 1", &lens.lensRadius1, 0.01, 10.0 );
			ImGui::SliderFloat( "Lens Radius 2", &lens.lensRadius2, 0.01, 10.0 );
			ImGui::SliderFloat( "Lens Thickness", &lens.lensThickness, 0.01, 10.0 );
			ImGui::SliderFloat( "Lens Rotation", &lens.lensRotate, -35.0, 35.0 );
			ImGui::SliderFloat( "Lens IOR", &lens.lensIOR, 0.0, 2.0 );
			ImGui::Separator();
			ImGui::ColorEdit3( "Red Wall Color", ( float * ) &scene.redWallColor, ImGuiColorEditFlags_PickerHueWheel );
			ImGui::ColorEdit3( "Green Wall Color", ( float * ) &scene.greenWallColor, ImGuiColorEditFlags_PickerHueWheel );
			ImGui::ColorEdit3( "Metallic Diffuse", ( float * ) &scene.metallicDiffuse, ImGuiColorEditFlags_PickerHueWheel );
			ImGui::Separator();
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( " Post " ) ) {
			// postprocessing parameters
			ImGui::SliderInt( "Dither Mode", &post.ditherMode, 0, 10 ); // however many there are - maybe use a dropdown for this
			ImGui::SliderInt( "Dither Method", &post.ditherMethod, 0, 10 ); // however many there are - maybe use a dropdown for this
			ImGui::SliderInt( "Dither Pattern", &post.ditherPattern, 0, 10 ); // however many there are - maybe use a dropdown for this
			ImGui::Separator();
			ImGui::SliderInt( "Tonemap Mode", &post.tonemapMode, 0, 8 ); // whatever the range ends up being
			ImGui::Separator();
			ImGui::SliderInt( "Depth Fog Mode", &post.depthMode, 0, 8 ); // whatever the range ends up being
			ImGui::SliderFloat( "Fog Depth Scalar", &post.depthScale, 0.01, 10.0 );
			ImGui::SliderFloat( "Gamma Correction", &post.gamma, 0.01, 3.0 );
			ImGui::SliderInt( "Display Type", &post.displayType, 0, 2 );

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	// performance monitoring
	float tileValues[ PERFORMANCEHISTORY ] = {};
	float fpsValues[ PERFORMANCEHISTORY ] = {};
	float tileAverage = 0;
	float fpsAverage = 0;
	for ( int n = 0; n < PERFORMANCEHISTORY; n++ ) {
		tileAverage += tileValues[ n ] = tileHistory[ n ];
		fpsAverage += fpsValues[ n ] = fpsHistory[ n ];
	}
	tileAverage /= float( PERFORMANCEHISTORY );
	fpsAverage /= float( PERFORMANCEHISTORY );
	char tileOverlay[ 100 ];
	char fpsOverlay[ 45 ];

	sprintf( tileOverlay, "avg %.2f tiles/update (%.2f ms/tile)", tileAverage, ( 1000.0f / fpsAverage ) / tileAverage );
	sprintf( fpsOverlay, "avg %.2f fps (%.2f ms)", fpsAverage, 1000.0f / fpsAverage );

	// absolute positioning within the window
	ImGui::SetCursorPosY( ImGui::GetWindowSize().y - 215 );
	ImGui::Text(" Performance Monitor");
	ImGui::SameLine();
	HelpMarker("Tiles are processed asynchronously to the frame update. This means that an arbitrary number of tiles may be processed in the space between frames, depending on hardware capabilities and the shader complexity, as configured. The program is designed to maintain ~60fps for responsiveness, regardless of what this hardware capability may be ( up to the point where the execution time of a single tile exceeds the total alotted frame time of 16ms ).");
	ImGui::Separator();
	ImGui::Text("  Tile History");
	ImGui::SetCursorPosX( 15 );

	// graph of tiles per frame, for the past $PERFORMANCEHISTORY frames
	ImGui::PlotLines( " ", tileValues, IM_ARRAYSIZE( tileValues ), 0, tileOverlay, -10.0f, float( host.tilePerFrameCap ) + 200.0f, ImVec2( ImGui::GetWindowSize().x - 30, 65 ) );
	ImGui::Text("  FPS History");

	// graph of time per frame, for the last $PERFORMANCEHISTORY frames
		// should stay flat (tm) at 60fps, given the structure of the pathtracing function ( abort on t >= 60fps equivalent )
	ImGui::SetCursorPosX( 15 );
	ImGui::PlotLines( " ", fpsValues, IM_ARRAYSIZE( fpsValues ), 0, fpsOverlay, -10.0f, 200.0f, ImVec2( ImGui::GetWindowSize().x - 30, 65 ) );
	ImGui::Text("  Current Sample Count: %d", fullscreenPasses );

	// finished with the settings window
	ImGui::End();

	// finish up the imgui stuff and put it in the framebuffer
	imguiFrameEnd();
}

void engine::handleEvents() {
	SDL_Event event;
	while ( SDL_PollEvent( &event ) ) {
		// imgui event handling
		ImGui_ImplSDL2_ProcessEvent( &event );

		// quit, but with a confirm window to make sure you mean it
		if ( ( event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE ) || ( event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_X1 ) ) {
			quitConfirm = !quitConfirm; // x1 is browser back on the mouse
		}

		pQuit = ( event.type == SDL_QUIT ) || // force quit conditions - window close or shift + escape
				( event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID( window ) ) ||
				( event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE && SDL_GetModState() & KMOD_SHIFT );

		// if ( event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_s )
		// 	basicScreenShot();	// do the big one via the menus, to avoid accidental trigger

		if ( event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_p ) {
			cout << to_string( core.viewerPosition );	// show current position of the viewer
		}

		if ( event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_r ) {
			resetAccumulators();
		}

		// quaternion based rotation via retained state in the basis vectors - much easier to use than the arbitrary euler angles
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_w ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ? -0.1f : -0.005f, core.basisX ); // basisX is the axis, therefore remains untransformed
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_s ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ?  0.1f :  0.005f, core.basisX );
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_a ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ? -0.1f : -0.005f, core.basisY ); // same as above, but basisY is the axis
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ?  0.1f :  0.005f, core.basisY );
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ? -0.1f : -0.005f, core.basisZ ); // and again for basisZ
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_e ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ?  0.1f :  0.005f, core.basisZ );
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
		}

		// f to reset basis, shift + f to reset basis and home to origin
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_f ) {
			if( SDL_GetModState() & KMOD_SHIFT ) {
				core.viewerPosition = glm::vec3( 0.0f, 0.0f, 0.0f );
			}
			// reset to default basis
			core.basisX = glm::vec3( 1.0f, 0.0f, 0.0f );
			core.basisY = glm::vec3( 0.0f, 1.0f, 0.0f );
			core.basisZ = glm::vec3( 0.0f, 0.0f, 1.0f );
		}

		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_UP ) {
			core.viewerPosition += ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisZ;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_DOWN ) {
			core.viewerPosition -= ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisZ;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RIGHT ) {
			core.viewerPosition += ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisX;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LEFT ) {
			core.viewerPosition -= ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisX;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PAGEUP ) {
			core.viewerPosition += ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisY;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PAGEDOWN ) {
			core.viewerPosition -= ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisY;
		}
	}
}

glm::ivec2 engine::getTile() {
	static std::vector< glm::ivec2 > offsets;
	static int listOffset = 0;
	static bool firstTime = true;
	std::random_device rd;
	std::mt19937 rngen( rd() );

	if ( firstTime ) { // construct the tile list
		firstTime = false;
		for( int x = 0; x <= WIDTH; x += TILESIZE ) {
			for( int y = 0; y <= HEIGHT; y += TILESIZE ) {
				offsets.push_back( glm::ivec2( x, y ) );
			}
		}
	} else { // check if the offset needs to be reset
		if ( ++listOffset == int( offsets.size() ) ) {
			listOffset = 0; fullscreenPasses++;
		}
	}
	// shuffle when listOffset is zero ( first iteration, and any subsequent resets )
	if ( !listOffset ) std::shuffle( offsets.begin(), offsets.end(), rngen );
	return offsets[ listOffset ];
}

// this pulls the texture data from the accumulator and saves it to a PNG image with a timestamp
void engine::basicScreenShot() {
	std::vector< GLfloat > imageAsFloats;
	imageAsFloats.resize( WIDTH * HEIGHT * 4, 0 );

	// belt and suspenders, what's 100ms between friends?
	SDL_Delay( 30 );
	glMemoryBarrier(  GL_ALL_BARRIER_BITS );
	SDL_Delay( 30 );
	glMemoryBarrier(  GL_ALL_BARRIER_BITS );

	// get the image data ( floating point accumulator )
	// glBindTexture( GL_TEXTURE_2D, colorAccumulatorTexture );
	// glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, &imageAsFloats[ 0 ] );

	std::vector< unsigned char > imageAsBytes;
	imageAsBytes.reserve( WIDTH * HEIGHT * 4 );

	// go from float to uint
	// for( unsigned int i = 0; i < imageAsFloats.size(); i += 4 ) {
	// 	// alpha channel is accumulator - not good data
	//
	//
	// 	// tonemapping needs to be done to bring the data into the expected range
	//
	//
	// 	imageAsBytes.push_back( std::clamp( imageAsFloats[ i + 0 ] * 255.0, 0.0, 255.0 ) );
	// 	imageAsBytes.push_back( std::clamp( imageAsFloats[ i + 1 ] * 255.0, 0.0, 255.0 ) );
	// 	imageAsBytes.push_back( std::clamp( imageAsFloats[ i + 2 ] * 255.0, 0.0, 255.0 ) );
	// 	imageAsBytes.push_back( 255 );
	// }

	glBindTexture( GL_TEXTURE_2D, displayTexture );
	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, &imageAsBytes[ 0 ] );

	// reorder the pixels, as the image coming from the GPU will be upside down
	std::vector< unsigned char > outputBytes;
	outputBytes.resize( WIDTH * HEIGHT * 4 );
	for ( int x = 0; x < WIDTH; x++ ) {
		for ( int y = 0; y < HEIGHT; y++ ) {
			for ( int c = 0; c < 4; c++ ) {
				outputBytes[ ( ( x + y * WIDTH ) * 4 ) + c ] = imageAsBytes[ ( x + ( HEIGHT - y - 1 ) * WIDTH ) * 4 + c ];
			}
		}
	}

	// get timestamp and save
	auto now = std::chrono::system_clock::now();
	auto in_time_t = std::chrono::system_clock::to_time_t( now );

	std::stringstream ss;
	ss << std::put_time(std::localtime(&in_time_t), "Screenshot-%Y-%m-%d %X") << ".png";
	std::string filename = ss.str();

	unsigned error;
	if ( ( error = lodepng::encode( filename.c_str(), outputBytes, WIDTH, HEIGHT ))) {
		std::cout << "encode error during save(\" " + filename + " \") " << error << ": " << lodepng_error_text( error ) << std::endl;
	}
}

// this is going to be a situation where we go from the regular render target, to a new render target, of some configured dimensions, up to what is supported by the driver
// try to figure out how to make it not lock up while it's running
const int result( int& val ){ glGetIntegerv( GL_MAX_TEXTURE_SIZE, &val ); return val; }
void engine::offlineScreenShot() {
	// GLint val;
	// static const int maxTextureSize = result( val );
	// cout << "Max texture dimension: " << val << endl;

	// set to max of screenshotDim and maximum value
	// host.screenshotDim = std::min( maxTextureSize, host.screenshotDim );
	//
	// cout << "Capturing at " << host.screenshotDim << " by " << static_cast< int > ( host.screenshotDim * ( float( HEIGHT ) / float( WIDTH ) ) ) << std::endl;

	// create the texture, same aspect ratio as preview texture, but with the larger dimensions
	// this is going to be configured in the menus and kept in the config structs
	// render the image into the

}
