#include "engine.h"

int main( int argc, char *argv[] ) {
	engine e;
	while( e.mainLoop() );
	return 0;
}
