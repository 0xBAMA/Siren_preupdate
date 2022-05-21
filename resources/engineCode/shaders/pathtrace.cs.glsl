#version 430 core
layout( local_size_x = 16, local_size_y = 16, local_size_z = 1 ) in;

layout( binding = 1, rgba32f ) uniform image2D accumulatorColor;
layout( binding = 2, rgba32f ) uniform image2D accumulatorNormalsAndDepth;
layout( binding = 3, rgba8ui ) uniform uimage2D blueNoise;

#define PI 3.1415926535897932384626433832795
#define AA 1 // each sample is actually 2*2 = 4 offset samples

// core rendering stuff
uniform ivec2	tileOffset;					// tile renderer offset for the current tile
uniform ivec2	noiseOffset;				// jitters the noise sample read locations
uniform int		maxSteps;						// max steps to hit
uniform int		maxBounces;					// number of pathtrace bounces
uniform float	maxDistance;				// maximum ray travel
uniform float understep;					// scale factor on distance, when added as raymarch step
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

// Jos Leys / Knighty
// https://www.shadertoy.com/view/XlVXzh
// vec2 wrap(vec2 x, vec2 a, vec2 s){
//     x -= s;
//     return (x-a*floor(x/a)) + s;
// }
//
// void TransA(inout vec3 z, inout float DF, float a, float b){
//     float iR = 1. / dot(z,z);
//     z *= -iR;
//     z.x = -b - z.x; z.y = a + z.y;
//     DF *= max(1.,iR);
// }
//
// float JosKleinian(vec3 z) {
//     float adjust = 6.28; // use this for time varying behavior
//
//     float box_size_x=1.;
//     float box_size_z=1.;
//
//     float KleinR = 1.94+0.05*abs(sin(-adjust*0.5));//1.95859103011179;
//     float KleinI = 0.03*cos(-adjust*0.5);//0.0112785606117658;
//     vec3 lz=z+vec3(1.), llz=z+vec3(-1.);
//     float d=0.; float d2=0.;
//
//     vec3 InvCenter=vec3(1.0,1.0,0.);
//     float rad=0.8;
//     z=z-InvCenter;
//     d=length(z);
//     d2=d*d;
//     z=(rad*rad/d2)*z+InvCenter;
//
//     float DE=1e10;
//     float DF = 1.0;
//     float a = KleinR;
//     float b = KleinI;
//     float f = sign(b)*1. ;
//     for (int i = 0; i < 69 ; i++)
//     {
//         z.x=z.x+b/a*z.y;
//         z.xz = wrap(z.xz, vec2(2. * box_size_x, 2. * box_size_z), vec2(- box_size_x, - box_size_z));
//         z.x=z.x-b/a*z.y;
//
//         //If above the separation line, rotate by 180° about (-b/2, a/2)
//         if  (z.y >= a * 0.5 + f *(2.*a-1.95)/4. * sign(z.x + b * 0.5)* (1. - exp(-(7.2-(1.95-a)*15.)* abs(z.x + b * 0.5))))
//         {z = vec3(-b, a, 0.) - z;}
//
//         //Apply transformation a
//         TransA(z, DF, a, b);
//
//         //If the iterated points enters a 2-cycle , bail out.
//         if(dot(z-llz,z-llz) < 1e-5) {break;}
//
//         //Store prévious iterates
//         llz=lz; lz=z;
//     }
//
//
//     float y =  min(z.y, a-z.y) ;
//     DE=min(DE,min(y,0.3)/max(DF,2.));
//     DE=DE*d2/(rad+d*DE);
//     return DE;
// }

vec4 QtSqr ( vec4 q ){
  return vec4 (2. * q.w * q.xyz, q.w * q.w - dot (q.xyz, q.xyz));
}
vec4 QtCub ( vec4 q ){
  float b;
  b = dot (q.xyz, q.xyz);
  return vec4 (q.xyz * (3. * q.w * q.w - b), q.w * (q.w * q.w - 3. * b));
}
float nHit; // escape term
float ObjDf ( vec3 p ){
  vec4 q, qq, c;
  vec2 b;
  float s, ss, ot;
  q = vec4 (p, 0.).yzwx;
  c = vec4 (0.2727, 0.6818, -0.2727, -0.0909);
  b = vec2 (0.45, 0.55);
  s = 0.;
  ss = 1.;
  ot = 100.;
  nHit = 0.;
  for (int j = 0; j < 256; j ++) {
    ++ nHit;
    qq = QtSqr (q);
    ss *= 9. * dot (qq, qq);
    q = QtCub (q) + c;
    ot = min (ot, length (q.wy - b) - 0.1);
    s = dot (q, q);
    if (s > 32.) break;
  }
  return min (ot, max (0.25 * log (s) * sqrt (s / ss) - 0.001, 0.));
}
float deFractal3(vec3 p){
    return max( ObjDf(p), p.y );
}


vec2 box_size = vec2(-0.40445, 0.34) * 2.;

//sphere inversion
bool SI=false;
vec3 InvCenter=vec3(0,1,1);
float rad=.8;

vec2 wrap ( vec2 x, vec2 a, vec2 s ) {
    x -= s;
    return (x - a * floor(x / a)) + s;
}

void TransA(inout vec3 z, inout float DF, float a, float b){
    float iR = 1. / dot(z,z);
    z *= -iR;
    z.x = -b - z.x; z.y = a + z.y;
    DF *= iR;//max(1.,iR);
}

float JosKleinian(vec3 z)
{
    float t = 0.;

    float KleinR = 1.5 + .39;
    float KleinI = (.55 * 2. - 1.);
    vec3 lz=z+vec3(1.), llz=z+vec3(-1.);
    float d=0.; float d2=0.;

    if (SI) {
        z=z-InvCenter;
        d=length(z);
        d2=d*d;
        z=(rad*rad/d2)*z+InvCenter;
    }

    float DE = 1e12;
    float DF = 1.;
    float a = KleinR;
    float b = KleinI;
    float f = sign(b) * .45;
    for (int i = 0; i < 80 ; i++)
    {
        z.x += b / a * z.y;
        z.xz = wrap(z.xz, box_size * 2., -box_size);
        z.x -= b / a * z.y;

        //If above the separation line, rotate by 180° about (-b/2, a/2)
        if  (z.y >= a * 0.5 + f *(2.*a-1.95)/4. * sign(z.x + b * 0.5)* (1. - exp(-(7.2-(1.95-a)*15.)* abs(z.x + b * 0.5))))
        {z = vec3(-b, a, 0.) - z;}

        //Apply transformation a
        TransA(z, DF, a, b);

        //If the iterated points enters a 2-cycle , bail out.
        if(dot(z-llz,z-llz) < 1e-5) {break;}

        //Store prévious iterates
        llz=lz; lz=z;
    }

    float y =  min(z.y, a - z.y);
    DE = min(DE, min(y, .3) / max(DF, 2.));
    if (SI) {
        DE = DE * d2 / (rad + d * DE);
    }

    return DE;
}



// Spherical Inversion Variant of Above
float deLoopy ( vec3 p ) {
	p = p.yzx;
  float adjust = 6.28; // use this for time varying behavior
  float box_size_x = 1.0;
  float box_size_z = 1.0;
  float KleinR = 1.95859103011179;
  float KleinI = 0.0112785606117658;
  vec3 lz = p + vec3( 1.0 ), llz = p + vec3( -1.0 );
  float d = 0.0; float d2 = 0.0;
  vec3 InvCenter = vec3( 1.0, 1.0, 0.0 );
  float rad = 0.8;
  p = p - InvCenter;
  d = length( p );
  d2 = d * d;
  p = ( rad * rad / d2 ) * p + InvCenter;
  float DE = 1e10;
  float DF = 1.0;
  float a = KleinR;
  float b = KleinI;
  float f = sign( b ) * 1.0;
  for ( int i = 0; i < 69 ; i++ ) {
    p.x = p.x + b / a * p.y;
    p.xz = wrap( p.xz, vec2( 2. * box_size_x, 2. * box_size_z ), vec2( -box_size_x, - box_size_z ) );
    p.x = p.x - b / a * p.y;
    if ( p.y >= a * 0.5 + f *( 2.0 * a - 1.95 ) / 4.0 * sign( p.x + b * 0.5 ) *
     ( 1.0 - exp( -( 7.2 - ( 1.95 - a ) * 15.0 )* abs( p.x + b * 0.5 ) ) ) ) {
      p = vec3( -b, a, 0.0 ) - p;
    } //If above the separation line, rotate by 180° about (-b/2, a/2)
    TransA( p, DF, a, b ); //Apply transformation a
    if ( dot( p - llz, p - llz ) < 1e-5 ) {
      break;
    } //If the iterated points enters a 2-cycle , bail out.
    llz = lz; lz = p; //Store previous iterates
  }

  float y =  min( p.y, a-p.y );
  DE = min( DE, min( y, 0.3 ) / max( DF, 2.0 ) );
  DE = DE * d2 / ( rad + d * DE );
  return DE;
}


// Hash based domain repeat snowflakes - Rikka 2 demo
float hash(float v){return fract(sin(v*22.9)*67.);}
mat2 rot(float a){float s=sin(a),c=cos(a);return mat2(c,s,-s,c);}
vec2 hexFold(vec2 p){return abs(abs(abs(p)*mat2(.866,.5,-.5,.866))*mat2(.5,-.866,.866,.5));}
float sdHex(vec3 p){p=abs(p);return max(p.z-.02,max((p.x*.5+p.y*.866),p.x)-.015);}
float deFlakes(vec3 p){
  float h=hash(floor(p.x)+floor(p.y)*133.3+floor(p.z)*166.6),o=13.0,s=1.+h;
	float t = 1.0;
  p=fract(p)-.5;
  p.y+=h*.4-.2;
  p.xz*=rot(t*(h+.8));
  p.yz*=rot(t+h*5.);
  h=hash(h);p.x+=h*.15;
  float l=dot(p,p);
  if(l>.1)return l*2.;
  for(int i=0;i<5;i++){
    p.xy=hexFold(p.xy);
    p.xy*=mat2(.866,-.5,.5,.866);
    p.x*=(s-h);
    h=hash(h);p.y-=h*.065-.015;p.y*=.8;
    p.z*=1.2;
    h=hash(h);p*=1.+h*.3;
    o=min(o,sdHex(p));
    h=hash(h);s=1.+h*2.;
  }
  return o;
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
	// float dWhiteWalls = min( dePlane( p, vec3( 0.0, 0.0, 1.0 ), 20.0 ),  dePlane( p, vec3( 0.0, 0.0, -1.0 ), 20.0 ) );
	// // float dWhiteWalls = dePlane( p, vec3( 0.0, 0.0, 1.0 ), 16.0 ); // back only
	// sceneDist = min( dWhiteWalls, sceneDist );
	// if( sceneDist == dWhiteWalls && dWhiteWalls <= epsilon ) {
	// 	hitpointColor = vec3( 0.78 );
	// 	hitpointSurfaceType = DIFFUSE;
	// }

	// fractal object
	// float dFractal = deFractal( p );
	// float dFractal = JosKleinian( p );
	float dFractal = deLoopy( p - vec3( 0.0, 1.0, 2.0 ) );
	sceneDist = min( dFractal, sceneDist );
	if( sceneDist == dFractal && dFractal <= epsilon ) {
		// hitpointColor = mix( vec3( 0.28, 0.42, 0.56 ), vec3( 0.55, 0.22, 0.1 ), ( p.z + 10.0 ) / 10.0 );
		// hitpointColor = vec3( 0.618 );
		hitpointColor = vec3( 0.618, 0.362, 0.04 );
		hitpointSurfaceType = SPECULAR;
	}

	// lens object
	// float dLens = deLens( p );
	// sceneDist = min( dLens, sceneDist );
	// if( sceneDist == dLens && dLens <= epsilon ) {
	// 	hitpointColor = vec3( 0.11 );
	// 	hitpointSurfaceType = REFRACTIVE;
	// }

	// cieling and floor
	// float dFloorCieling = min( dePlane( p, vec3( 0.0, -1.0, 0.0 ), 10.0 ),  dePlane( p, vec3( 0.0, 1.0, 0.0 ), 7.5 ) );
	float dFloorCieling = dePlane( p, vec3( 0.0, 1.0, 0.0 ), 10.0 );
	sceneDist = min( dFloorCieling, sceneDist );
	if( sceneDist == dFloorCieling && dFloorCieling <= epsilon ) {
		hitpointColor = vec3( 0.9 );
		hitpointSurfaceType = DIFFUSE;
	}

	pMod2( p.xz, vec2( 2.0 ) );
	// float dLightBall = deFlakes( p );
	float dLightBall = distance( p, vec3( 0.0, 7.5, 0.0 ) ) - 0.1618;
	sceneDist = min( dLightBall, sceneDist );
	if( sceneDist == dLightBall && dLightBall <= epsilon ) {
		hitpointColor = vec3( 0.8 );
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
			e = vec2( 1.0, -1.0 ) * epsilon / 10.0;
			return normalize( e.xyy * de( p + e.xyy ) + e.yyx * de( p + e.yyx ) + e.yxy * de( p + e.yxy ) + e.xxx * de( p + e.xxx ) );
			break;

			case 1: // from iq = more efficient, 4 DE evaluations
				e = vec2( epsilon, 0.0 ) / 10.0;
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
		dTotal += dQuery * understep;
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

	// jitter ray starting point
	float startJitterScaleFactor = 0.1;
	vec4 blue = blueNoiseReference( location );
	rayOrigin += rayDirection * blue.x * startJitterScaleFactor;

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
		rayOrigin += ( 2.0 + 0.1 * blue.y - 0.05 ) * epsilon * hitNormal; // jitter slightly

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

#define RANDOM
vec2 getRandomOffset( int n ){
	// weyl sequence from http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/ and https://www.shadertoy.com/view/4dtBWH
#ifdef UNIFORM
		return fract( vec2( 0.0 ) + vec2(n*12664745, n*9560333)/exp2(24.));	// integer mul to avoid round-off
#endif
#ifdef UNIFORM2
		return fract( vec2( 0.0 ) + float(n)*vec2(0.754877669, 0.569840296));
#endif
// wang hash random offsets
#ifdef RANDOM
		return vec2( randomFloat(), randomFloat() ) ;
#endif
}

void storeNormalAndDepth( vec3 normal, float depth ) {
	// blend with history and imageStore
	vec4 prevResult = imageLoad( accumulatorNormalsAndDepth, location );
	vec4 blendResult = mix( prevResult, vec4( normal, depth ), 1.0 / sampleCount );
	imageStore( accumulatorNormalsAndDepth, location, blendResult );
}

vec3 pathtraceSample( ivec2 location, int n ) {
	vec3  cResult = vec3( 0.0 );
	vec3  nResult = vec3( 0.0 );
	float dResult = 0.0;

	// at AA = 2, this is 4 samples per invocation
	for( int x = 0; x < AA; x++ ) {
		for( int y = 0; y < AA; y++ ) {
			// pixel offset + mapped position
			// vec2 offset = vec2( x + randomFloat(), y + randomFloat() ) / float( AA ) - 0.5; // previous method
			vec2 offset = ( getRandomOffset( n ) + vec2( x, y ) ) / float( AA );
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

	vec3 blendResult = mix( prevResult.rgb, pathtraceSample( location, int( sampleCount ) ), 1.0 / sampleCount );
	imageStore( accumulatorColor, location, vec4( blendResult, sampleCount ) );
}
