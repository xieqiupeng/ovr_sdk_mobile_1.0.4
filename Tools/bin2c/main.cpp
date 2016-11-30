
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#if defined(WIN32)
	#include <io.h>
	#include <conio.h>
	#include <direct.h>
#else
	#include <stdio.h>
	#include <unistd.h>
	inline errno_t fopen_s(FILE **file, const char *filename, const char *mode)
	{
		*file = fopen(filename, mode);
		return *file ? 0 : -1;
	}
	inline size_t fread_s(void *buffer, size_t bufferSize, size_t elementSize, size_t count, FILE *stream)
	{
		return fread(buffer, bufferSize, elementSize, stream);
	}
	inline int _access(const char *path, int mode)
	{
		return access(path, mode);
	}
#endif

#include "../../3rdParty/stb/src/stb_image.h"
#include "../../3rdParty/stb/src/stb_image_write.h"

class CBuffer
{
public:
	CBuffer() :
		buffer( NULL ),
		size( 0 )
	{
	}
	~CBuffer()
	{
		delete [] buffer;
		size = 0;
	}
	unsigned char * buffer;
	size_t          size;
};

bool LoadFile( char const * filename, CBuffer & buffer )
{
	FILE * f = NULL;
	errno_t error = fopen_s( &f, filename, "rb" );
	if ( error != 0 )
	{
		return false;
	}

	fseek( f, 0, SEEK_END );
	buffer.size = ftell( f );

	buffer.buffer = new unsigned char[buffer.size];

	fseek( f, 0, SEEK_SET );
	size_t countRead = fread_s( buffer.buffer, buffer.size, buffer.size, 1, f ); 

	fclose( f );
	f = NULL;

	if ( countRead != 1 )
	{
		delete [] buffer.buffer;
		buffer.buffer = NULL;
		buffer.size = 0;
		return false;
	}

	return true;
}

bool WriteCFile( char const * outFileName, CBuffer const & buffer, const char * bufferName )
{
	// delete if it exists
	if ( _access( outFileName, 0 ) == 0 )
	{
		if ( _access( outFileName, 02 ) != 0 )
		{
			printf( "Access violation writing '%s'\n", outFileName );
			return false;
		}
	}

	remove( outFileName );

	FILE * f = NULL;
	errno_t error = fopen_s( &f, outFileName, "wb" );
	if ( error != 0 )
	{
		return false;
	}

	fprintf( f, "static const size_t %sSize = %u;\r\n", bufferName, (unsigned int)buffer.size );
	fprintf( f, "static unsigned char %sData[] =\r\n{\r\n", bufferName );

	const int MAX_LINE_WIDTH = 100;	// 4 + 16 * 6
	const int TAB_SIZE = 4;
	int charCount = TAB_SIZE;
	for ( size_t i = 0; i < buffer.size; i++ )
	{
		bool const lastChar = ( i == buffer.size - 1 );
		fprintf( f, "%s0x%02x%s", 
					charCount == TAB_SIZE ? "\t" : "",
					buffer.buffer[ i ], 
					lastChar ? "" : ", " );
		charCount += 6;
		if ( lastChar || charCount + 6 > MAX_LINE_WIDTH )
		{
			fprintf( f, "\r\n" );
			charCount = TAB_SIZE;
		}
	}

	fprintf( f, "};\r\n" );

	fclose( f );

	return true;
}

int main( int const argc, char const * argv[] )
{
	if ( argc < 3 )
	{
		printf( "bin2c v1.0 by Nelno the Amoeba\n" );
		printf( "Reads in a binary file and outputs a C file for embedding the binary file\n" );
		printf( "in code.\n" );
		printf( "USAGE: bin2c <binary file name> <out file name> [options]\n" );
		printf( "options:\n" );
		printf( "-name <name> : Name of the C buffer.\n" );
		printf( "-image       : First convert image to RGBA raw data.\n" );
		return 0;
	}

	char const * binaryFileName = argv[1];
	const char * outFileName = argv[2];
	const char * bufferName = "buffer";

	CBuffer buffer;

	if ( !LoadFile( binaryFileName, buffer ) )
	{
		printf( "Error loading binary file '%s'\n", binaryFileName );
		return 1;
	}

	for ( int i = 3; i < argc; i++ )
	{
		if ( strcmp( argv[i], "-name" ) == 0 )
		{
			if ( i + 1 < argc )
			{
				bufferName = argv[i + 1];
			}
		}
		else if ( strcmp( argv[i], "-image" ) == 0 )
		{
			printf( "Converting %s to raw RGBA data...\n", binaryFileName );
			int w, h, c;
			stbi_uc * image = stbi_load_from_memory( buffer.buffer, static_cast< int >( buffer.size ), &w, &h, &c, 4 );
			delete [] buffer.buffer;
			buffer.size = w * h * 4 * sizeof( unsigned char );
			buffer.buffer = new unsigned char[buffer.size];
			memcpy( buffer.buffer, image, buffer.size );
			stbi_image_free( image );
		}
		else
		{
			printf( "Invalid parameter %s\n", argv[i] );
			return 1;
		}
	}

	if ( !WriteCFile( outFileName, buffer, bufferName ) )
	{
		printf( "Error writing C file '%s'\n", outFileName );
		return 1;
	}

	return 0;
}
