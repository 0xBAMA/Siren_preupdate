#version 430 core
layout( local_size_x = 16, local_size_y = 16, local_size_z = 1 ) in;

layout( binding = 1, rgba32f ) uniform image2D accumulatorColor;
layout( binding = 2, rgba32f ) uniform image2D accumulatorNormalsAndDepth;
layout( binding = 3, rgba8ui ) uniform uimage2D blueNoise;

#define PI 3.1415926535897932384626433832795
#define AA 2 // each sample is actually 2*2 = 4 offset samples

// core rendering stuff
uniform ivec2	tileOffset;					// tile renderer offset for the current tile
uniform ivec2	noiseOffset;				// jitters the noise sample read locations
uniform int		maxSteps;						// max steps to hit
uniform int		maxBounces;					// number of pathtrace bounces
uniform float	maxDistance;				// maximum ray travel
uniform float	epsilon;						// how close is considered a surface hit
uniform int		normalMethod;				// selector for normal computation method
uniform float	focusDistance;			// for thin lens approx
uniform float	thinLensIntensity;	// scalar on the thin lens DoF effect
uniform float	FoV;								// field of view
uniform float	exposure;						// exposure adjustment
uniform vec3	viewerPosition;			// position of the viewer
uniform vec3	basisX;							// x basis vector
uniform vec3	basisY;							// y basis vector
uniform vec3	basisZ;							// z basis vector
uniform int		wangSeed;						// integer value used for seeding the wang hash rng

// lens parameters
uniform float lensScaleFactor;		// scales the lens DE
uniform float lensRadius1;				// radius of the sphere for the first side
uniform float lensRadius2;				// radius of the sphere for the second side
uniform float lensThickness;			// offset between the two spheres
uniform float lensRotate;					// rotating the displacement offset betwee spheres
uniform float lensIOR;						// index of refraction for the lens

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

vec2 randomInUnitDisk() {
	return randomUnitVector().xy;
}

mat3 rotate3D( float angle, vec3 axis ) {
	vec3 a = normalize( axis );
	float s = sin( angle );
	float c = cos( angle );
	float r = 1.0 - c;
	return mat3(
		a.x * a.x * r + c,
		a.y * a.x * r + a.z * s,
		a.z * a.x * r - a.y * s,
		a.x * a.y * r - a.z * s,
		a.y * a.y * r + c,
		a.z * a.y * r + a.x * s,
		a.x * a.z * r + a.y * s,
		a.y * a.z * r - a.x * s,
		a.z * a.z * r + c
	);
}


float fOpIntersectionRound(float a, float b, float r) {
	vec2 u = max(vec2(r + a,r + b), vec2(0));
	return min(-r, max (a, b)) + length(u);
}

// Repeat in two dimensions
vec2 pMod2(inout vec2 p, vec2 size) {
	vec2 c = floor((p + size*0.5)/size);
	p = mod(p + size*0.5,size) - size*0.5;
	return c;
}



vec3 hitpointColor = vec3( 0.0 );

// 0 nohit
#define NOHIT 0
// 1 diffuse
#define DIFFUSE 1
// 2 specular
#define SPECULAR 2
// 3 emissive
#define EMISSIVE 3
// 4 refractive
#define REFRACTIVE 4

// identifier for the hit surface
int hitpointSurfaceType = 0;

// tracking global state wrt state of inside/outside lens material
	// requires management that the lens material does not intersect with itself
float refractState = 1.0; // multiply by the lens distance estimate, to invert when inside a refractive object
float deLens( vec3 p ){
	// lens SDF
	p /= lensScaleFactor;
	float dFinal;
	float center1 = lensRadius1 - lensThickness / 2.0;
	float center2 = -lensRadius2 + lensThickness / 2.0;
	vec3 pRot = rotate3D( 0.1 * lensRotate, vec3( 1.0 ) ) * p;
	float sphere1 = distance( pRot, vec3( 0.0, center1, 0.0 ) ) - lensRadius1;
	float sphere2 = distance( pRot, vec3( 0.0, center2, 0.0 ) ) - lensRadius2;

	// dFinal = fOpIntersectionRound( sphere1, sphere2, 0.03 );
	dFinal = max( sphere1, sphere2 );
	return dFinal * lensScaleFactor;
}

void pR( inout vec2 p, float a ) {
	p = cos( a ) * p + sin( a ) * vec2( p.y, -p.x );
}

float deFractal(vec3 p){
	float scalar = 0.3;
	p /= scalar;

	const int iterations = 20;
	float d = ( p.z * scalar ) * 2.0;
	// float d = -2.; // vary this parameter, range is like -20 to 20
	p=p.yxz;
	pR(p.yz, 1.570795);
	p.x += 6.5;
	p.yz = mod(abs(p.yz)-.0, 20.) - 10.;
	float scale = 1.25;
	p.xy /= (1.+d*d*0.0005);

	float l = 0.;
	for (int i=0; i < iterations; i++) {
		p.xy = abs(p.xy);
		p = p*scale + vec3(-3. + d*0.0095,-1.5,-.5);
		pR(p.xy,0.35-d*0.015);
		pR(p.yz,0.5+d*0.02);
		vec3 p6 = p*p*p; p6=p6*p6;
		l =pow(p6.x + p6.y + p6.z, 1./6.);
	}
	return ( l * pow( scale, -float( iterations ) ) - 0.15 ) * scalar;
}

float deFractal2( vec3 p ){
	float s = 2.;
	float e = 0.;
	for(int j=0;++j<7;)
		p.xz=abs(p.xz)-2.3,
		p.z>p.x?p=p.zyx:p,
		p.z=1.5-abs(p.z-1.3+sin(p.z)*.2),
		p.y>p.x?p=p.yxz:p,
		p.x=3.-abs(p.x-5.+sin(p.x*3.)*.2),
		p.y>p.x?p=p.yxz:p,
		p.y=.9-abs(p.y-.4),
		e=12.*clamp(.3/min(dot(p,p),1.),.0,1.)+
		2.*clamp(.1/min(dot(p,p),1.),.0,1.),
		p=e*p-vec3(7,1,1),
		s*=e;
	return length(p)/s;
}

float dePlane( vec3 p, vec3 normal, float distanceFromOrigin ) {
	return dot( p, normal ) + distanceFromOrigin;
}

// surface distance estimate for the whole scene
float de( vec3 p ) {
	// init nohit, far from surface, no diffuse color
	hitpointSurfaceType = NOHIT;
	float sceneDist = 1000.0;
	hitpointColor = vec3( 0.0 );

	// cornell box
	// red wall ( left )
	float dRedWall = dePlane( p, vec3( 1.0, 0.0, 0.0 ), 10.0 );
	sceneDist = min( dRedWall, sceneDist );
	if( sceneDist == dRedWall && dRedWall <= epsilon ) {
		hitpointColor = vec3( 1.0, 0.0, 0.0 );
		hitpointSurfaceType = DIFFUSE;
	}

	// green wall ( right )
	float dGreenWall = dePlane( p, vec3( -1.0, 0.0, 0.0 ), 10.0 );
	sceneDist = min( dGreenWall, sceneDist );
	if( sceneDist == dGreenWall && dGreenWall <= epsilon ) {
		hitpointColor = vec3( 0.0, 1.0, 0.0 );
		hitpointSurfaceType = DIFFUSE;
	}

	// white walls ( front and back )
	// float dWhiteWalls = min( dePlane( p, vec3( 0.0, 0.0, 1.0 ), 10.0 ),  dePlane( p, vec3( 0.0, 0.0, -1.0 ), 10.0 ) );
	float dWhiteWalls = dePlane( p, vec3( 0.0, 0.0, 1.0 ), 10.0 ); // back only
	sceneDist = min( dWhiteWalls, sceneDist );
	if( sceneDist == dWhiteWalls && dWhiteWalls <= epsilon ) {
		hitpointColor = vec3( 0.78 );
		hitpointSurfaceType = DIFFUSE;
	}

	// fractal object
	float dFractal = deFractal( p );
	sceneDist = min( dFractal, sceneDist );
	if( sceneDist == dFractal && dFractal <= epsilon ) {
		// hitpointColor = mix( vec3( 0.28, 0.42, 0.56 ), vec3( 0.55, 0.22, 0.1 ), ( p.z + 10.0 ) / 10.0 );
		// hitpointColor = vec3( 0.618 );
		hitpointColor = vec3( 0.618, 0.362, 0.04 );
		hitpointSurfaceType = SPECULAR;
	}

	// lens object
	float dLens = deLens( p );
	sceneDist = min( dLens, sceneDist );
	if( sceneDist == dLens && dLens <= epsilon ) {
		hitpointColor = vec3( 0.11 );
		hitpointSurfaceType = REFRACTIVE;
	}

	// cieling and floor
	float dFloorCieling = min( dePlane( p, vec3( 0.0, -1.0, 0.0 ), 10.0 ),  dePlane( p, vec3( 0.0, 1.0, 0.0 ), 7.5 ) );
	sceneDist = min( dFloorCieling, sceneDist );
	if( sceneDist == dFloorCieling && dFloorCieling <= epsilon ) {
		hitpointColor = vec3( 0.9 );
		hitpointSurfaceType = DIFFUSE;
	}

	pMod2( p.xz, vec2( 2.5 ) );
	float dLightBall = distance( p, vec3( 0.0, 7.5, 0.0 ) ) - 0.1618;
	sceneDist = min( dLightBall, sceneDist );
	if( sceneDist == dLightBall && dLightBall <= epsilon ) {
		hitpointColor = vec3( 1.0 );
		hitpointSurfaceType = EMISSIVE;
	}

	// wang hash seeded scattered emissive spheres of random colors in the negative space? maybe refractive, not sure
		// need to make sure that the seed is constant, and the existing seed is cached and restored, if I'm going to do this

	return sceneDist;
}

// fake AO, computed from SDF
float calcAO( in vec3 pos, in vec3 nor ) {
	float occ = 0.0;
	float sca = 1.0;
	for( int i = 0; i < 5; i++ ) {
		float h = 0.001 + 0.15 * float( i ) / 4.0;
		float d = de( pos + h * nor );
		occ += ( h - d ) * sca;
		sca *= 0.95;
	}
	return clamp( 1.0 - 1.5 * occ, 0.0, 1.0 );
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



ivec2 location = ivec2( 0, 0 );	// 2d location, pixel coords
vec3 colorSample( vec3 rayOrigin_in, vec3 rayDirection_in ) {

// #define DEBUG
#ifdef DEBUG
// debug output for testing
	float rayDistance = raymarch( rayOrigin_in, rayDirection_in );
	vec3 pHit = rayOrigin_in + rayDistance * rayDirection_in;
	if( de( rayOrigin_in + rayDistance * rayDirection_in ) < epsilon )
		return hitpointColor;
	else
		return vec3( 0.0 );
#endif

	vec3 rayOrigin = rayOrigin_in, previousRayOrigin;
	vec3 rayDirection = rayDirection_in, previousRayDirection;
	vec3 finalColor = vec3( 0.0 );
	vec3 throughput = vec3( 1.0 );

	// loop to max bounces
	for( int bounce = 0; bounce < maxBounces; bounce++ ) {
		float dResult = raymarch( rayOrigin, rayDirection );

		// cache previous values of rayOrigin, rayDirection, and get new hit position
		previousRayOrigin = rayOrigin;
		previousRayDirection = rayDirection;
		rayOrigin = rayOrigin + dResult * rayDirection;

		// surface normal at the new hit position
		vec3 hitNormal = normal( rayOrigin );

		// bump rayOrigin along the normal to prevent false positive hit on next bounce
			// now you are at least epsilon distance from the surface, so you won't immediately hit
		rayOrigin += 2.0 * epsilon * hitNormal;

	// these are mixed per-material
		// construct new rayDirection vector, diffuse reflection off the surface
		vec3 reflectedVector = reflect( previousRayDirection, hitNormal );

		vec3 randomVectorDiffuse = normalize( ( 1.0 + epsilon ) * hitNormal + randomUnitVector() );
		vec3 randomVectorSpecular = normalize( ( 1.0 + epsilon ) * hitNormal + mix( reflectedVector, randomUnitVector(), 0.1 ) );


		// currently just implementing diffuse and emissive behavior
			// eventually add different ray behaviors for each material here
		switch ( hitpointSurfaceType ) {

			case EMISSIVE:
				finalColor += throughput * hitpointColor;
				break;

			case DIFFUSE:
				rayDirection = randomVectorDiffuse;
				throughput *= hitpointColor; // attenuate throughput by surface albedo
				break;

			case SPECULAR:
				rayDirection = mix( randomVectorDiffuse, randomVectorSpecular, 0.7 );
				throughput *= hitpointColor;
				break;

			case REFRACTIVE:
				// ray refracts, instead of bouncing
				// for now, perfect reflector
				rayDirection = reflectedVector;

				// flip the sign to invert the lens material distance estimate, because the ray is either entering or leaving a refractive medium
				refractState = -1.0 * refractState;
				break;

			default:
				break;

		}

		// russian roulette termination - chance for ray to quit early
		float maxChannel = max( throughput.r, max( throughput.g, throughput.b ) );
		if ( randomFloat() > maxChannel ) break;

		// russian roulette compensation term
		throughput *= 1.0 / maxChannel;
	}

	return finalColor;
}


void storeNormalAndDepth( vec3 normal, float depth ) {
	// blend with history and imageStore
	vec4 prevResult = imageLoad( accumulatorNormalsAndDepth, location );
	vec4 blendResult = mix( prevResult, vec4( normal, depth ), 1.0 / sampleCount );
	imageStore( accumulatorNormalsAndDepth, location, blendResult );
}

vec3 pathtraceSample( ivec2 location ) {
	vec3  cResult = vec3( 0.0 );
	vec3  nResult = vec3( 0.0 );
	float dResult = 0.0;

	// at AA = 2, this is 4 samples per invocation
	for( int x = 0; x < AA; x++ ) {
		for( int y = 0; y < AA; y++ ) {
			// pixel offset + mapped position
			vec2 offset = vec2( x + randomFloat(), y + randomFloat() ) / float( AA ) - 0.5;
			vec2 halfScreenCoord = vec2( imageSize( accumulatorColor ) / 2.0 );
			vec2 mappedPosition = ( vec2( location + offset ) - halfScreenCoord ) / halfScreenCoord;

			// aspect ratio
			float aspectRatio = float( imageSize( accumulatorColor ).x ) / float( imageSize( accumulatorColor ).y );

			// ray origin + direction
			vec3 rayDirection = normalize( aspectRatio * mappedPosition.x * basisX + mappedPosition.y * basisY + ( 1.0 / FoV ) * basisZ );
			vec3 rayOrigin    = viewerPosition + rayDirection * 0.1 * blueNoiseReference( location ).x;

			// thin lens DoF - adjust view vectors to converge at focusDistance
				// this is a small adjustment to the ray origin and direction - not working correctly - need to revist this
			// vec3 focuspoint = rayOrigin + ( ( rayDirection * focusDistance ) / dot( rayDirection, basisZ ) );
			// vec2 diskOffset = thinLensIntensity * randomInUnitDisk();
			// rayOrigin += diskOffset.x * basisX + diskOffset.y * basisY + thinLensIntensity * randomFloat() * basisZ;
			// rayDirection = normalize( focuspoint - rayOrigin );

			// get depth and normals - think about special handling for refractive hits
			float distanceToFirstHit = raymarch( rayOrigin, rayDirection );
			storeNormalAndDepth( normal( rayOrigin + distanceToFirstHit * rayDirection ), distanceToFirstHit );

			// get the result for a ray
			cResult += colorSample( rayOrigin, rayDirection );
		}
	}
	float normalizeTerm = float( AA * AA );
	return ( cResult / normalizeTerm ) * exposure;
}

void main() {
	location = ivec2( gl_GlobalInvocationID.xy ) + tileOffset;
	seed = location.x * 1973 + location.y * 9277 + wangSeed;

	if( !boundsCheck( location ) ) return; // abort on out of bounds
	vec4 prevResult = imageLoad( accumulatorColor, location );
	sampleCount = prevResult.a + 1.0;

	vec3 blendResult = mix( prevResult.rgb, pathtraceSample( location ), 1.0 / sampleCount );
	imageStore( accumulatorColor, location, vec4( blendResult, sampleCount ) );
}
