

MSVC 2013
---------

Before compiling, download and install the Autodesk FBX SDK 2015.1

Copy the SDK from:

C:\Program Files\Autodesk\FBX\FBX SDK\2015.1\

to a folder:

\3rdParty\FBXSDK\2015.1\

relative to where the Oculus Mobile SDK is installed (noting that the
source directory 'FBX SDK' differs from the destination directory
'FBXSDK'). 

Load the FbxConvert.sln to compile the source.


Mac OSX
-------

Before compiling download and install the Autodesk FBX SDK 2015.1 to:

/Applications/Autodesk/FBX SDK/2015.1

Compile the source with make:

make -f Makefile_Mac -j10

If your copy of FBX resides at a different location you can also optionally
set the environment variable FBXSDK_ROOT to its location. e.g.

make -f Makefile_Mac -j10 FBXSDK_ROOT=/path/to/fbx/2015.1


Linux
-----

Before compiling download and install the Autodesk FBX SDK 2015.1

Compile the source with make:

make -f Makefile_Linux
