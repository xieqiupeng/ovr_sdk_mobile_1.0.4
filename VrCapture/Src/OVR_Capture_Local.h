/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_Local.h
Content     :   Internal Capture API
Created     :   January, 2015
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#ifndef OVR_CAPTURE_LOCAL_H
#define OVR_CAPTURE_LOCAL_H

#include <OVR_Capture_Config.h>
#include <OVR_Capture_Types.h>

#include <stdarg.h>

#include <cstring>
#include <cstdio>

namespace OVR
{
namespace Capture
{

	bool TryLockConnection(void);
	bool TryLockConnection(const CaptureFlag feature);
	void UnlockConnection(void);

	// Dynamically acquire a Label hash from a c-string... must be guarded by TryLockConnection/UnlockConnection!!!
	UInt32 ComputeLabelHash(const Label &label);

#if defined(OVR_CAPTURE_HAS_GLES3)
	void OnConnectGLES3(void);
	void OnDisconnectGLES3(void);
#endif

	static inline void MemorySet(void *dest, int ch, std::size_t count)
	{
		std::memset(dest, ch, count);
	}

	static inline void MemoryCopy(void *dest, const void *src, std::size_t count)
	{
		std::memcpy(dest, src, count );
	}

	static inline int FormatStringV(char *buffer, size_t bufferSize, const char *format, va_list args)
	{
		// TODO: return std::vsnprintf(buffer, bufferSize, format, args);
		//       but for some reason its not finding it even though we build in C++11 mode... gnustl bug?
	#if defined(OVR_CAPTURE_WINDOWS)
		return vsprintf_s(buffer, bufferSize, format, args);
	#else
		return vsnprintf(buffer, bufferSize, format, args);
	#endif
	}

	static inline int FormatString(char *buffer, size_t bufferSize, const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		const int ret = FormatStringV(buffer, bufferSize, format, args);
		va_end(args);
		return ret;
	}

	static inline void StringCopy(char *dest, const char *source, size_t size)
	{
	#if defined(OVR_CAPTURE_WINDOWS)
		strncpy_s(dest, size, source, size);
	#else
		std::strncpy(dest, source, size);
	#endif
	}

	static inline size_t StringLength(const char *str)
	{
		return std::strlen(str);
	}

} // namespace Capture
} // namespace OVR

#endif
