/************************************************************************************

Filename    :   Reflection.h
Content     :   Functions and declarations for introspection and reflection of C++ objects.
Created     :   11/16/2015
Authors     :   Jonathan E. Wright

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#if !defined( OVR_Reflection_h )
#define OVR_Reflection_h

#include "Kernel/OVR_String.h"
#include "Kernel/OVR_Lexer.h"
#include "Kernel/OVR_Array.h"

namespace OVR {

struct ovrTypeInfo;
struct ovrMemberInfo;
class ovrLocale;
class ovrReflection;

//==============================================================================================
// Parsing
//==============================================================================================

class ovrParseResult 
{
public:
	ovrParseResult()
		: Result( ovrLexer::LEX_RESULT_OK )
		, Error()
	{
	}

	ovrParseResult( ovrLexer::ovrResult const result, char const * fmt, ... );

	operator bool () const { return Result == ovrLexer::LEX_RESULT_OK; }

	char const *	GetErrorText() const { return Error.ToCStr(); }

private:
	ovrLexer::ovrResult	Result;
	String				Error;
};

ovrParseResult ExpectPunctuation( const char * name, ovrLexer & lex, const char * expected );

ovrParseResult ParseBool( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * /*atomicInfo*/, void * outPtr, size_t const arraySize );
ovrParseResult ParseInt( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * /*atomicInfo*/, void * outPtr, size_t const arraySize );
ovrParseResult ParseFloat( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * /*atomicInfo*/, void * outPtr, size_t const arraySize );
ovrParseResult ParseDouble( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * /*atomicInfo*/, void * outPtr, size_t const arraySize );
ovrParseResult ParseEnum( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * atomicInfo, void * outPtr, size_t const arraySize );
ovrParseResult ParseBitFlags( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * atomicInfo, void * outPtr, size_t const arraySize );
ovrParseResult ParseTypesafeNumber_int( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * atomicInfo, void * outPtr, size_t const arraySize );
ovrParseResult ParseTypesafeNumber_long_long( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * atomicInfo, void * outPtr, size_t const arraySize );
ovrParseResult ParseString( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * /*atomicInfo*/, void * outPtr, size_t const arraySize );
ovrParseResult ParseFloatVector( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * /*atomicInfo*/, void * outPtr, size_t const arraySize );
ovrParseResult ParseArray( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * arrayTypeInfo, void * objPtr, size_t const arraySize );
ovrParseResult ParseObject( ovrReflection & refl, ovrLocale const & locale, const char * name, ovrLexer & lex, ovrTypeInfo const * objectTypeInfo, void * objPtr, size_t const arraySize );

//==============================================================================================
// Reflection data types
//==============================================================================================

typedef ovrParseResult (*ParseFn_t)( ovrReflection & refl, ovrLocale const & locale, char const * name, ovrLexer & lex, ovrTypeInfo const * typeInfo, void * outPtr, const size_t arraySize );
typedef void * (*CreateFn_t)( void * placementBuffer );
typedef void (*ResizeArrayFn_t)( void * objPtr, const int newSize );
typedef void (*SetArrayElementFn_t)( void * objPtr, const int index, void * elementPtr );

enum class ovrArrayType : char
{
	NONE,
	OVR_OBJECT,		// OVR::Array< T* >
	OVR_POINTER,	// OVR::Array< T >
	C_OBJECT,		// T[]
	C_POINTER		// T*[]
};

struct ovrEnumInfo
{
	char const *	Name;
	int				Value;
};

struct ovrTypeInfo
{
	char const *			TypeName;
	const char *			ParentTypeName;	// this would need to be an array to support multiple inheritance
	size_t					Size;
	ovrEnumInfo	const *		EnumInfos;
	ParseFn_t				ParseFn;
	CreateFn_t				CreateFn;
	ResizeArrayFn_t			ResizeArrayFn;
	SetArrayElementFn_t		SetArrayElementFn;
	ovrArrayType			ArrayType;	/// FIXME: only a member can have an array type... a type itself can't be an array? NOT TRUE FOR CLASSES
	bool					Abstract;	// true if class is abstract
	ovrMemberInfo const *	MemberInfo;
};

enum class ovrTypeOperator : char
{
	NONE,
	POINTER,
	REFERENCE,
	ARRAY,
	MAX
};

struct ovrMemberInfo
{
	char const *		MemberName;
	const char *		TypeName;
	ovrTypeInfo const *	VarType;		// cached pointer to type?
	ovrTypeOperator		Operator;
	intptr_t			Offset;
	size_t				ArraySize;		// If an array, this is the number of items in the array, otherwise it's 0.
};

//==============================================================================================
// Reflection Functions
//==============================================================================================

class ovrReflection
{
public:
	static ovrReflection *			Create();
	static void						Destroy( ovrReflection * & r );

	void							Init();
	void							Shutdown();
	// Add an additional list of types. The list must be terminated by a a
	void							AddTypeInfoList( ovrTypeInfo const * list );

	ovrMemberInfo const *			FindMemberReflectionInfoRecursive( ovrTypeInfo const * objectTypeInfo, const char * memberName );
	ovrMemberInfo const *			FindMemberReflectionInfo( ovrMemberInfo const * arrayOfMemberType, const char * memberName );
	ovrTypeInfo const *				FindTypeInfo( char const * typeName );

protected:
	static ovrTypeInfo const *		StaticFindTypeInfo( ovrTypeInfo const * list, char const * typeName );


private:
	Array< ovrTypeInfo const * >	TypeInfoLists;
	// can only be allocated and deleted by ovrReflection::Create and ovrReflection::Destroy
	ovrReflection() { };	
	virtual	~ovrReflection() { }
};
	
}	// namespace OVR

#endif // OVR_Reflection_h