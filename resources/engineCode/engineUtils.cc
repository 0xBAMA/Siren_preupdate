#include "engine.h"

bool engine::mainLoop() {
	render();						// render with the current mode
	postprocess();					// colorAccumulatorTexture -> displayTexture
	mainDisplayBlit();				// fullscreen triangle copying the image
	imguiPass();					// do all the GUI stuff
	SDL_GL_SwapWindow( window );	// swap the double buffers to present
	handleEvents();					// handle input events
	return !pQuit;					// break loop in main.cc when pQuit becomes true
}

void engine::render() {
	// different rendering modes - preview until pathtrace is triggered
	glUseProgram( pathtraceShader );

	// 2d blue noise read offset
	updateNoiseOffsets();

	// send the uniforms
	pathtraceUniformUpdate();

	// used for the wang hash seeding
	static std::default_random_engine gen;
	static std::uniform_int_distribution< int > dist( 0, std::numeric_limits< int >::max() / 4 );

	// seeding the wang rng in the shader - shader uses both the screen location and this value
	int value = dist( gen );
	glUniform1i( glGetUniformLocation( pathtraceShader, "wangSeed" ), value );

	int mode = 0;
	switch ( host.currentMode ) {
		case renderMode::previewColor:	mode = 1; break;
		case renderMode::previewNormal:	mode = 2; break;
		case renderMode::previewDepth:	mode = 3; break;
		default: break;
	}
	glUniform1i( glGetUniformLocation( pathtraceShader, "modeSelect" ), mode );

	// pathtrace happens in tiles
	if ( host.currentMode == renderMode::pathtrace ) {

		if ( host.rendererRequiresUpdate == true ) {
			host.rendererRequiresUpdate = false;
			resetAccumulators();
		}

		GLuint64 startTime, checkTime;
		GLuint queryID[ 2 ];
		glGenQueries( 2, queryID );
		glQueryCounter( queryID[ 0 ], GL_TIMESTAMP );

		// get startTime
		GLint startTimeAvailable = 0;
		while( !startTimeAvailable )
		glGetQueryObjectiv( queryID[ 0 ], GL_QUERY_RESULT_AVAILABLE, &startTimeAvailable );
		glGetQueryObjectui64v( queryID[ 0 ], GL_QUERY_RESULT, &startTime );

		int tilesCompleted = 0;
		float looptime = 0.0f;

		while ( 1 ) {
			glm::ivec2 tile = getTile(); // get a tile offset + send it
			glUniform2i( glGetUniformLocation( pathtraceShader, "tileOffset" ), tile.x, tile.y );

			// render the specified tile - dispatch
			glDispatchCompute( host.tileSize / 16, host.tileSize / 16, 1 );
			// glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
			glMemoryBarrier( GL_ALL_BARRIER_BITS );
			tilesCompleted++;

			// check time, wait for query to be ready
			glQueryCounter( queryID[ 1 ], GL_TIMESTAMP );
			GLint checkTimeAvailable = 0;
			while ( !checkTimeAvailable ) {
				glGetQueryObjectiv( queryID[ 1 ], GL_QUERY_RESULT_AVAILABLE, &checkTimeAvailable );
			}
			glGetQueryObjectui64v( queryID[ 1 ], GL_QUERY_RESULT, &checkTime );

			// break if duration exceeds 16 ms ( 60fps + a small margin ) - query units are nanoseconds
			looptime = ( checkTime - startTime ) / 1e6f; // get milliseconds
			if ( looptime > ( 16.0f ) || tilesCompleted >= host.tilePerFrameCap ) {
				// cout << tilesCompleted << " tiles in " << looptime << " ms, avg " << looptime / tilesCompleted << " ms/tile" << endl;
				break;
			}
		}
		fpsHistory.push_back( 1000.0f / looptime );
		fpsHistory.pop_front();

		tileHistory.push_back( tilesCompleted );
		tileHistory.pop_front();

	// preview happens in a one shot fullscreen pass
	} else if ( host.currentMode != renderMode::pathtrace && host.rendererRequiresUpdate == true ) {
		host.rendererRequiresUpdate = false; // don't run again till state changes

		// quick raymarch, only runs when movement has happened since last render event
			// don't need to update the history deques, as they will not be displayed


		// run for every pixel on the screen
		glDispatchCompute( ( WIDTH + 15 ) / 16, ( HEIGHT + 15 ) / 16, 1 );
		glMemoryBarrier( GL_ALL_BARRIER_BITS );
	}
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
	// core
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

	// lens
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensScaleFactor" ), lens.lensScaleFactor );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensRadius1" ), lens.lensRadius1 );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensRadius2" ), lens.lensRadius2 );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensThickness" ), lens.lensThickness );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensRotate" ), lens.lensRotate );
	glUniform1f( glGetUniformLocation( pathtraceShader, "lensIOR" ), lens.lensIOR );

	// scene
	glUniform3f( glGetUniformLocation( pathtraceShader, "redWallColor" ), scene.redWallColor.x, scene.redWallColor.y, scene.redWallColor.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "greenWallColor" ), scene.greenWallColor.x, scene.greenWallColor.y, scene.greenWallColor.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "whiteWallColor" ), scene.whiteWallColor.x, scene.whiteWallColor.y, scene.whiteWallColor.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "floorCielingColor" ), scene.floorCielingColor.x, scene.floorCielingColor.y, scene.floorCielingColor.z );
	glUniform3f( glGetUniformLocation( pathtraceShader, "metallicDiffuse" ), scene.metallicDiffuse.x, scene.metallicDiffuse.y, scene.metallicDiffuse.z );
}

void engine::postprocessUniformUpdate () {
	// all postprocess parameters
	glUniform1i( glGetUniformLocation( postprocessShader, "ditherMode" ), post.ditherMode );
	glUniform1i( glGetUniformLocation( postprocessShader, "ditherMethod" ), post.ditherMethod );
	glUniform1i( glGetUniformLocation( postprocessShader, "ditherPattern" ), post.ditherPattern );
	glUniform1i( glGetUniformLocation( postprocessShader, "tonemapMode" ), post.tonemapMode );
	glUniform1i( glGetUniformLocation( postprocessShader, "depthMode" ), post.depthMode );
	glUniform1f( glGetUniformLocation( postprocessShader, "depthScale" ), post.depthScale );
	glUniform1f( glGetUniformLocation( postprocessShader, "gamma" ), post.gamma );
	glUniform1i( glGetUniformLocation( postprocessShader, "displayType" ), post.displayType );
}

void engine::postprocess () {
	// tonemapping and dithering, as configured in the GUI
	glUseProgram( postprocessShader );

	// send associated uniforms
	postprocessUniformUpdate();

	glDispatchCompute( ( WIDTH + 31 ) / 32, ( HEIGHT + 31 ) / 32, 1 );
	glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT ); // sync
}

void engine::mainDisplayBlit () {
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

void engine::resetAccumulators () {
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
	host.fullscreenPasses = 0; // reset sample count
	cout << "Accumulator Buffer has been reinitialized" << endl;
}

void engine::imguiPass () {
#define UPDATECHECK host.rendererRequiresUpdate=host.rendererRequiresUpdate||ImGui::IsItemEdited()

	// start the imgui frame
	imguiFrameStart();

	// show the demo window
	static bool showDemoWindow = false;
	if ( showDemoWindow ) {
		ImGui::ShowDemoWindow( &showDemoWindow );
	}

	// show quit confirm window
	quitConf( &quitConfirm );

	// Tabbed window for the controls categories of parameters
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar;
	ImGui::Begin( "Settings", NULL, flags );

	ImGui::Text( " Parameters " );
	if ( ImGui::BeginTabBar( "Config Sections", ImGuiTabBarFlags_None ) ) {
		if ( ImGui::BeginTabItem( " Host " ) ) {
			ImGui::SliderInt( "Screenshot Width", &host.screenshotDim, 1000, maxTextureSizeCheck );
			ImGui::SliderInt( "Screenshot Samples", &host.numSamplesScreenshot, 1, 512 );
			ImGui::Separator();
			ImGui::SliderInt( "Tile Per Frame Cap", &host.tilePerFrameCap, 1, 3000 );

			static int pickt = 1;
			ImGui::RadioButton( "Preview Color", &pickt, 1 ); UPDATECHECK;
			ImGui::SameLine();
			ImGui::RadioButton( "Preview Normal", &pickt, 2 ); UPDATECHECK;
			ImGui::SameLine();
			ImGui::RadioButton( "Preview Depth", &pickt, 3 ); UPDATECHECK;
			ImGui::SameLine();
			ImGui::RadioButton( "Pathtrace", &pickt, 0 ); UPDATECHECK;
			switch ( pickt ) {
				case 0: host.currentMode = renderMode::pathtrace; break;
				case 1: host.currentMode = renderMode::previewColor; break;
				case 2: host.currentMode = renderMode::previewNormal; break;
				case 3: host.currentMode = renderMode::previewDepth; break;
				default: break;
			}

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
			ImGui::SliderInt( "Max Raymarch Steps", &core.maxSteps, 1, 500 ); UPDATECHECK;
			ImGui::SliderInt( "Max Light Bounces", &core.maxBounces, 1, 50 );
			ImGui::SliderFloat( "Max Raymarch Distance", &core.maxDistance, 0.0f, 200.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Raymarch Understep", &core.understep, 0.1f, 1.0f );
			ImGui::SliderFloat( "Raymarch Epsilon", &core.epsilon, 0.0001f, 0.1f, "%.4f" ); UPDATECHECK;
			ImGui::Separator();
			ImGui::SliderFloat( "Exposure", &core.exposure, 0.1f, 3.6f ); UPDATECHECK;
			// ImGui::SliderFloat( "Thin Lens Focus Distance", &core.focusDistance, 0.0f, 200.0f );
			// ImGui::SliderFloat( "Thin Lens Effect Intensity", &core.thinLensIntensity, 0.0f, 5.0f );
			ImGui::Separator();
			ImGui::SliderInt( "SDF Normal Method", &core.normalMethod, 1, 3 ); UPDATECHECK;
			ImGui::SliderFloat( "Field of View", &core.FoV, 0.01f, 2.5f ); UPDATECHECK;
			ImGui::Separator();
			ImGui::SliderFloat( "Viewer X", &core.viewerPosition.x, -20.0f, 20.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Viewer Y", &core.viewerPosition.y, -20.0f, 20.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Viewer Z", &core.viewerPosition.z, -20.0f, 20.0f ); UPDATECHECK;

			// have it tell what the current set of basis vectors is

			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( " Lens / Model " ) ) {
			// lens geometry parameters
			ImGui::SliderFloat( "Lens Scale Factor", &lens.lensScaleFactor, 0.001f, 2.5f ); UPDATECHECK;
			ImGui::SliderFloat( "Lens Radius 1", &lens.lensRadius1, 0.01f, 10.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Lens Radius 2", &lens.lensRadius2, 0.01f, 10.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Lens Thickness", &lens.lensThickness, 0.01f, 10.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Lens Rotation", &lens.lensRotate, -35.0f, 35.0f ); UPDATECHECK;
			ImGui::SliderFloat( "Lens IOR", &lens.lensIOR, 0.0f, 2.0f );
			ImGui::Separator();
			ImGui::ColorEdit3( "Red Wall Color", ( float * ) &scene.redWallColor, ImGuiColorEditFlags_PickerHueWheel );
			ImGui::ColorEdit3( "Green Wall Color", ( float * ) &scene.greenWallColor, ImGuiColorEditFlags_PickerHueWheel );
			ImGui::ColorEdit3( "White Walls Color", ( float * ) &scene.whiteWallColor, ImGuiColorEditFlags_PickerHueWheel );
			ImGui::ColorEdit3( "Floor/Cieling Color", ( float * ) &scene.floorCielingColor, ImGuiColorEditFlags_PickerHueWheel );
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
			ImGui::SliderFloat( "Fog Depth Scalar", &post.depthScale, 0.01f, 10.0f );
			ImGui::SliderFloat( "Gamma Correction", &post.gamma, 0.01f, 3.0f );
			ImGui::SliderInt( "Display Type", &post.displayType, 0, 2 );
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if ( host.currentMode == renderMode::pathtrace ) {
		// performance monitoring
		float tileValues[ host.performanceHistory ] = {};
		float fpsValues[ host.performanceHistory ] = {};
		float tileAverage = 0;
		float fpsAverage = 0;
		for ( int n = 0; n < host.performanceHistory; n++ ) {
			tileAverage += tileValues[ n ] = tileHistory[ n ];
			fpsAverage += fpsValues[ n ] = fpsHistory[ n ];
		}
		tileAverage /= float( host.performanceHistory );
		fpsAverage /= float( host.performanceHistory );
		char tileOverlay[ 100 ];
		char fpsOverlay[ 45 ];

		const float msPerTile = ( 1000.0f / fpsAverage ) / tileAverage;
		const float pixelsPerMs = host.tileSize * host.tileSize / ( msPerTile );

		sprintf( tileOverlay, "avg %.2f tiles/update ( %.2f ms / tile, %.2f pixels / ms )", tileAverage, msPerTile, pixelsPerMs );
		sprintf( fpsOverlay, "avg %.2f fps ( %.2f ms )", fpsAverage, 1000.0f / fpsAverage );

		// absolute positioning within the window
		ImGui::SetCursorPosY( ImGui::GetWindowSize().y - 215 );
		ImGui::Text( " Performance Monitor" );
		ImGui::SameLine();
		HelpMarker( "Tiles are processed asynchronously to the frame update. This means that an arbitrary number of tiles may be processed in the space between frames, depending on hardware capabilities and the shader complexity, as configured. The program is designed to maintain ~60fps for responsiveness, regardless of what this hardware capability may be ( up to the point where the execution time of a single tile exceeds the total alotted frame time of 16ms )." );
		ImGui::Separator();
		ImGui::Text( "  Tile History" );
		ImGui::SetCursorPosX( 15 );

		// graph of tiles per frame, for the past $host.performanceHistory frames
		ImGui::PlotLines( " ", tileValues, IM_ARRAYSIZE( tileValues ), 0, tileOverlay, -10.0f, float( host.tilePerFrameCap ) + 200.0f, ImVec2( ImGui::GetWindowSize().x - 30, 65 ) );
		ImGui::Text( "  FPS History" );

		// graph of time per frame, for the last $host.performanceHistory frames
			// should stay flat (tm) at 60fps, given the structure of the pathtracing function ( abort on t >= 60fps equivalent )
		ImGui::SetCursorPosX( 15 );
		ImGui::PlotLines( " ", fpsValues, IM_ARRAYSIZE( fpsValues ), 0, fpsOverlay, -10.0f, 200.0f, ImVec2( ImGui::GetWindowSize().x - 30, 65 ) );
		ImGui::Text( "  Current Sample Count: %d", host.fullscreenPasses );
	}

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
			host.rendererRequiresUpdate = true;
		}

		// quaternion based rotation via retained state in the basis vectors - much easier to use than the arbitrary euler angles
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_w ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ? -0.1f : -0.005f, core.basisX ); // basisX is the axis, therefore remains untransformed
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_s ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ?  0.1f :  0.005f, core.basisX );
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_a ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ? -0.1f : -0.005f, core.basisY ); // same as above, but basisY is the axis
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ?  0.1f :  0.005f, core.basisY );
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisZ = ( rot * glm::vec4( core.basisZ, 0.0f ) ).xyz();
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ? -0.1f : -0.005f, core.basisZ ); // and again for basisZ
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_e ) {
			glm::quat rot = glm::angleAxis( SDL_GetModState() & KMOD_SHIFT ?  0.1f :  0.005f, core.basisZ );
			core.basisX = ( rot * glm::vec4( core.basisX, 0.0f ) ).xyz();
			core.basisY = ( rot * glm::vec4( core.basisY, 0.0f ) ).xyz();
			host.rendererRequiresUpdate = true;
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
			host.rendererRequiresUpdate = true;
		}

		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_UP ) {
			core.viewerPosition += ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisZ;
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_DOWN ) {
			core.viewerPosition -= ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisZ;
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RIGHT ) {
			core.viewerPosition += ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisX;
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LEFT ) {
			core.viewerPosition -= ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisX;
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PAGEUP ) {
			core.viewerPosition += ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisY;
			host.rendererRequiresUpdate = true;
		}
		if( event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_PAGEDOWN ) {
			core.viewerPosition -= ( SDL_GetModState() & KMOD_SHIFT ? 0.07f : 0.005f ) * core.basisY;
			host.rendererRequiresUpdate = true;
		}
	}
}

glm::ivec2 engine::getTile () {
	static std::vector< glm::ivec2 > offsets;
	static int listOffset = 0;
	std::random_device rd;
	std::mt19937 rngen( rd() );

	if ( host.tileSizeUpdated == true ) { // construct the tile list ( runs at frame 0 and again any time the value changes )
		host.tileSizeUpdated = false;
		for ( int x = 0; x <= WIDTH; x += host.tileSize ) {
			for ( int y = 0; y <= HEIGHT; y += host.tileSize ) {
				offsets.push_back( glm::ivec2( x, y ) );
			}
		}
	} else { // check if the offset needs to be reset, this means a full pass has been completed
		if ( ++listOffset == int( offsets.size() ) ) {
			listOffset = 0; host.fullscreenPasses++;
		}
	}
	// shuffle when listOffset is zero ( first iteration, and any subsequent resets )
	if ( !listOffset ) std::shuffle( offsets.begin(), offsets.end(), rngen );
	return offsets[ listOffset ];
}

// this pulls the texture data from the accumulator and saves it to a PNG image with a timestamp
void engine::basicScreenShot () {
	std::vector< GLfloat > imageAsFloats;
	imageAsFloats.resize( WIDTH * HEIGHT * 4, 0 );

	// belt and suspenders, what's 100ms between friends?
	SDL_Delay( 30 );
	glMemoryBarrier( GL_ALL_BARRIER_BITS );
	SDL_Delay( 30 );
	glMemoryBarrier( GL_ALL_BARRIER_BITS );

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
	ss << std::put_time( std::localtime( &in_time_t ), "Screenshot-%Y-%m-%d %X" ) << ".png";
	std::string filename = ss.str();

	unsigned error;
	if ( ( error = lodepng::encode( filename.c_str(), outputBytes, WIDTH, HEIGHT ) ) ) {
		std::cout << "encode error during save( \"" + filename + "\" ) " << error << ": " << lodepng_error_text( error ) << std::endl;
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

	// cout << "Capturing at " << host.screenshotDim << " by " << static_cast< int > ( host.screenshotDim * ( float( HEIGHT ) / float( WIDTH ) ) ) << std::endl;

	// create the texture, same aspect ratio as preview texture, but with the larger dimensions
	// this is going to be configured in the menus and kept in the config structs
	// render the image into another image target that's kept on the GPU

}
