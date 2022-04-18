#version 430 core
layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;

layout( binding = 0, rgba8ui ) uniform uimage2D display;
layout( binding = 1, rgba32f ) uniform image2D accumulatorColor;
layout( binding = 2, rgba32f ) uniform image2D accumulatorNormal;

void main() {
	// this isn't tiled - it may need to be, for when rendering larger resolution screenshots
	ivec2 location = ivec2( gl_GlobalInvocationID.xy );

	// Color comes in at 32-bit per channel precision
	vec4 toStore = imageLoad( accumulatorColor, location );

	// RGB is normal, A is depth value, 32-bit per channel
	vec4 normalAndDepth = imageLoad( accumulatorNormal, location );

	// do any postprocessing work, store back in display texture
		// this is things like:
		//	- depth fog
		//	- tonemapping
		//	- dithering

	imageStore( display, location, uvec4( toStore.xyz * 255.0, 255 ) );
}
