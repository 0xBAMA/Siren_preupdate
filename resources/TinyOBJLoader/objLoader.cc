// TinyOBJLoader - This has to be included in a .cc file
#define TINYOBJLOADER_IMPLEMENTATION
// #define TINYOBJLOADER_USE_DOUBLE
#include "objLoader.h"

// tinyobj callbacks
//  user_data is passed in as void, then cast as 'objLoader' class to push
//  vertices, normals, texcoords, index, material info
void vertex_cb( void *user_data, float x, float y, float z, float w ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	t->vertices.push_back( glm::vec4( x, y, z, w ) );
}

void normal_cb( void *user_data, float x, float y, float z ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	t->normals.push_back( glm::vec3( x, y, z ) );
}

void texcoord_cb( void *user_data, float x, float y, float z ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	t->texcoords.push_back( glm::vec3( x, y, z ) );
}

void index_cb( void *user_data, tinyobj::index_t *indices, int num_indices ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );

	if ( num_indices == 3 ) { // this is a triangle
	// OBJ uses 1-indexing, convert to 0-indexing
	t->triangleIndices.push_back( glm::ivec3( indices[ 0 ].vertex_index - 1,
																						indices[ 1 ].vertex_index - 1,
																						indices[ 2 ].vertex_index - 1 ) );
	t->normalIndices.push_back( glm::ivec3( indices[ 0 ].normal_index - 1,
																					indices[ 1 ].normal_index - 1,
																					indices[ 2 ].normal_index - 1 ) );
	t->texcoordIndices.push_back( glm::ivec3( indices[ 0 ].texcoord_index - 1,
																						indices[ 1 ].texcoord_index - 1,
																						indices[ 2 ].texcoord_index - 1 ) );
	}
	// lines, points have a different number of indicies
	//  might want to handle these
}

void usemtl_cb( void *user_data, const char *name, int material_idx ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	( void ) t;
}

void mtllib_cb( void *user_data, const tinyobj::material_t *materials, int num_materials ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	( void ) t;
}

void group_cb( void *user_data, const char **names, int num_names ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	( void ) t;
}

void object_cb( void *user_data, const char *name ) {
	objLoader *t = reinterpret_cast< objLoader * >( user_data );
	( void ) t;
}

// this is where the callbacks are set up and used
void objLoader::LoadOBJ( std::string fileName ) {
	tinyobj::callback_t cb;
	cb.vertex_cb = vertex_cb;
	cb.normal_cb = normal_cb;
	cb.texcoord_cb = texcoord_cb;
	cb.index_cb = index_cb;
	cb.usemtl_cb = usemtl_cb;
	cb.mtllib_cb = mtllib_cb;
	cb.group_cb = group_cb;
	cb.object_cb = object_cb;

	std::string warn;
	std::string err;

	std::ifstream ifs( fileName.c_str() );
	tinyobj::MaterialFileReader mtlReader( "." );

	bool ret = tinyobj::LoadObjWithCallback( ifs, cb, this, &mtlReader, &warn, &err );

	if ( !warn.empty() ) {
		std::cout << "WARN: " << warn << std::endl;
	}

	if ( !err.empty() ) {
		std::cerr << err << std::endl;
	}

	if ( !ret ) {
		std::cerr << "Failed to parse .obj at location " << fileName << std::endl;
	}

	cout << "vertex list length: " << vertices.size() << endl;
	cout << "normal list length: " << normals.size() << endl;
	cout << "texcoord list length: " << texcoords.size() << endl;

	cout << "vertex index list length: " << triangleIndices.size() << endl;
	cout << "normal index length: " << normalIndices.size() << endl;
	cout << "texcoord index length: " << texcoordIndices.size() << endl;
}
