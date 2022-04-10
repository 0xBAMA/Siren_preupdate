#ifndef OBJLOAD
#define OBJLOAD

#include "tiny_obj_loader.h"
#include "../engineCode/includes.h"

class objLoader {
public:
	objLoader(){}
	objLoader( std::string fileName ){
		LoadOBJ( fileName );
	}

	void LoadOBJ( std::string fileName );

	// OBJ data ( per mesh )
	// this may vary in length
	std::vector< glm::vec4 > vertices;
	std::vector< glm::vec3 > normals;
	std::vector< glm::vec3 > texcoords;

	// these should all be the same length, the number of triangles
	std::vector< glm::ivec3 > triangleIndices;
	std::vector< glm::ivec3 > normalIndices;
	std::vector< glm::ivec3 > texcoordIndices;
};

#endif
