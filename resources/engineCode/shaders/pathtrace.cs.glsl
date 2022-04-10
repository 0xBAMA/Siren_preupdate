#version 430 core
layout( local_size_x = 16, local_size_y = 16, local_size_z = 1 ) in;

layout( binding = 1, rgba32f ) uniform image2D accumulatorColor;

// add another texture to hold normals in R, G, B and depth in A
layout( binding = 2, rgba32f ) uniform image2D accumulatorNormalsAndDepth;

layout( binding = 3, rgba8ui ) uniform uimage2D blueNoise;

#define PI 3.1415926535897932384626433832795
#define AA 2 // each sample is actually 2^2 = 4 offset samples

// core rendering stuff
uniform ivec2 tileOffset;       // tile renderer offset for the current tile
uniform ivec2 noiseOffset;      // jitters the noise sample read locations
uniform int   maxSteps;         // max steps to hit
uniform int   maxBounces;       // number of pathtrace bounces
uniform float maxDistance;      // maximum ray travel
uniform float epsilon;          // how close is considered a surface hit
uniform int   normalMethod;     // selector for normal computation method
uniform float focusDistance;    // for thin lens approx
uniform float FoV;              // field of view
uniform float exposure;         // exposure adjustment
uniform vec3  viewerPosition;   // position of the viewer
uniform vec3  basisX;           // x basis vector
uniform vec3  basisY;           // y basis vector
uniform vec3  basisZ;           // z basis vector

// lens parameters
uniform float lensScaleFactor;  // scales the lens DE
uniform float lensRadius1;      // radius of the sphere for the first side
uniform float lensRadius2;      // radius of the sphere for the second side
uniform float lensThickness;    // offset between the two spheres
uniform float lensRotate;       // rotating the displacement offset betwee spheres
uniform float lensIOR;          // index of refraction for the lens

// global state
float sampleCount = 0.0;

bool boundsCheck( ivec2 loc ) { // used to abort off-image samples
	ivec2 bounds = ivec2( imageSize( accumulatorColor ) ).xy;
	return ( loc.x < bounds.x && loc.y < bounds.y );
}

vec4 blueNoiseReference( ivec2 location ) { // jitter source
	location += noiseOffset;
	location.x = location.x % imageSize( blueNoise ).x;
	location.y = location.y % imageSize( blueNoise ).y;
	return vec4( imageLoad( blueNoise, location ) / 255. );
}

// random utilites
uint seed = 0;
uint wangHash() {
	seed = uint( seed ^ uint( 61 ) ) ^ uint( seed >> uint( 16 ) );
	seed *= uint( 9 );
	seed = seed ^ ( seed >> 4 );
	seed *= uint( 0x27d4eb2d );
	seed = seed ^ ( seed >> 15 );
	return seed;
}

float randomFloat() {
	return float( wangHash() ) / 4294967296.0;
}

vec3 randomUnitVector() {
	float z = randomFloat() * 2.0f - 1.0f;
	float a = randomFloat() * 2. * PI;
	float r = sqrt( 1.0f - z * z );
	float x = r * cos( a );
	float y = r * sin( a );
	return vec3( x, y, z );
}

vec3 randomInUnitDisk() {
	return vec3( randomUnitVector().xy, 0. );
}





// surface distance estimate for the lens
float lensDE( vec3 p ) {
	return 0.0; // currently placeholder
}

// normalized gradient of the SDF - 3 different methods
vec3 lensNorm( vec3 p ) {
	vec2 e;
	switch( normalMethod ) {

		case 0: // tetrahedron version, unknown original source - 4 DE evaluations
			e = vec2( 1.0, -1.0 ) * epsilon;
			return normalize( e.xyy * lensDE( p + e.xyy ) + e.yyx * lensDE( p + e.yyx ) + e.yxy * lensDE( p + e.yxy ) + e.xxx * lensDE( p + e.xxx ) );
			break;

		case 1: // from iq = more efficient, 4 DE evaluations
			e = vec2( epsilon, 0.0 );
			return normalize( vec3( lensDE( p ) ) - vec3( lensDE( p - e.xyy ), lensDE( p - e.yxy ), lensDE( p - e.yyx ) ) );
			break;

		case 2: // from iq - less efficient, 6 DE evaluations
			e = vec2( epsilon, 0.0 );
			return normalize( vec3( lensDE( p + e.xyy ) - lensDE( p - e.xyy ), lensDE( p + e.yxy ) - lensDE( p - e.yxy ), lensDE( p + e.yyx ) - lensDE( p - e.yyx ) ) );
			break;

		default:
			break;
	}
}



// surface distance estimate for the whole scene
float de(vec3 p){
	return min(.65-length(fract(p+.5)-.5),p.y+.2);
}

// normalized gradient of the SDF - 3 different methods
vec3 normal( vec3 p ) {
	vec2 e;
	switch( normalMethod ) {

		case 0: // tetrahedron version, unknown original source - 4 DE evaluations
			e = vec2( 1.0, -1.0 ) * epsilon;
			return normalize( e.xyy * de( p + e.xyy ) + e.yyx * de( p + e.yyx ) + e.yxy * de( p + e.yxy ) + e.xxx * de( p + e.xxx ) );
			break;

			case 1: // from iq = more efficient, 4 DE evaluations
				e = vec2( epsilon, 0.0 );
				return normalize( vec3( de( p ) ) - vec3( de( p - e.xyy ), de( p - e.yxy ), de( p - e.yyx ) ) );
				break;

			case 2: // from iq - less efficient, 6 DE evaluations
				e = vec2( epsilon, 0.0 );
				return normalize( vec3( de( p + e.xyy ) - de( p - e.xyy ), de( p + e.yxy ) - de( p - e.yxy ), de( p + e.yyx ) - de( p - e.yyx ) ) );
				break;

			default:
				break;
	}
}





// raymarches to the next hit
float raymarch( vec3 origin, vec3 direction ) {
	float dQuery = 0.0;
	float dTotal = 0.0;
	for ( int steps = 0; steps < maxSteps; steps++ ) {
		vec3 pQuery = origin + dTotal * direction;
		dQuery = de( pQuery );
		dTotal += dQuery;
		if ( dTotal > maxDistance || abs( dQuery ) < epsilon ) {
			break;
		}
	}
	return dTotal;
}


ivec2 location;
vec3 colorSample( vec3 rayOrigin, vec3 rayDirection ) {
	// loop to max bounces

	float distance = raymarch( rayOrigin, rayDirection );
	return normal( rayOrigin + distance * rayDirection );
	// return vec3( de( rayOrigin + 2.0 * rayDirection ) );
}


void storeNormalAndDepth( vec3 normal, float depth ) {
	// blend with history and imageStore
}

vec3 pathtraceSample( ivec2 location ) {
	vec3  cResult = vec3( 0. );
	vec3  nResult = vec3( 0. );
	float dResult = 0.;

	for( int x = 0; x < AA; x++ ) {
		for( int y = 0; y < AA; y++ ) {
			// pixel offset + mapped position
			vec2 offset = vec2( x + randomFloat(), y + randomFloat() ) / float( AA ) - 0.5;
			vec2 halfScreenCoord = vec2( imageSize( accumulatorColor ) / 2. );
			vec2 mappedPosition = ( vec2( location + offset ) - halfScreenCoord ) / halfScreenCoord;

			// aspect ratio
			float aspectRatio = float( imageSize( accumulatorColor ).x ) / float( imageSize( accumulatorColor ).y );

			// ray origin + direction
			vec3 rayDirection = normalize( aspectRatio * mappedPosition.x * basisX + mappedPosition.y * basisY + ( 1. / FoV ) * basisZ );
			vec3 rayOrigin    = viewerPosition;

			// thin lens DoF - adjust view vectors to converge at focusDistance

			// get depth and normals - move this to the colorSample function? - think about special handling for refractive hits

			// get the result for a ray
			cResult += colorSample( rayOrigin, rayDirection );
		}
	}
	float normalizeTerm = float( AA * AA );
	return ( cResult / normalizeTerm ) * exposure;
}

void main() {
	location = ivec2( gl_GlobalInvocationID.xy ) + tileOffset;

	if( !boundsCheck( location ) ) return; // abort on out of bounds
	vec4 prevResult = imageLoad( accumulatorColor, location );
	sampleCount = prevResult.a + 1.0;

	vec3 blendResult = mix( prevResult.rgb, pathtraceSample( location ), 1.0 / sampleCount );
	imageStore( accumulatorColor, location, vec4( blendResult, sampleCount ) );
}
