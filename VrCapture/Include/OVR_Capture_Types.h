/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_Types.h
Content     :   Oculus performance capture library.
Created     :   January, 2015
Notes       :   We intentionally don't use cstdint types because the API will remain
                C++03 compatible at least for the near future.
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#ifndef OVR_CAPTURE_TYPES_H
#define OVR_CAPTURE_TYPES_H

#include <OVR_Capture_Config.h>

namespace OVR
{
namespace Capture
{

	// We choose very specific types for the network protocol...
	typedef unsigned long long UInt64;
	typedef unsigned int       UInt32;
	typedef unsigned short     UInt16;
	typedef unsigned char      UInt8;

	typedef signed long long   Int64;
	typedef signed int         Int32;
	typedef signed short       Int16;
	typedef signed char        Int8;

	OVR_CAPTURE_STATIC_ASSERT(sizeof(UInt64) == 8);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(UInt32) == 4);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(UInt16) == 2);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(UInt8)  == 1);

	OVR_CAPTURE_STATIC_ASSERT(sizeof(Int64)  == 8);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(Int32)  == 4);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(Int16)  == 2);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(Int8)   == 1);

	OVR_CAPTURE_STATIC_ASSERT(sizeof(float)  == 4);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(double) == 8);

	struct Quaternion
	{
		Quaternion(void)
		{
		}
		Quaternion(float _x, float _y, float _z, float _w) : 
			x(_x), y(_y), z(_z), w(_w)
		{
		}

		float x, y, z, w;
	};

	struct Vector3
	{
		Vector3(void)
		{
		}
		Vector3(float _x, float _y, float _z) :
			x(_x), y(_y), z(_z)
		{
		}

		float x, y, z;
	};

	OVR_CAPTURE_STATIC_ASSERT(sizeof(Quaternion) == 16);
	OVR_CAPTURE_STATIC_ASSERT(sizeof(Vector3)    == 12);

	typedef void (*OnConnectFunc)(UInt32 connectionFlags);
	typedef void (*OnDisconnectFunc)(void);

	enum CaptureFlag
	{
		Enable_CPU_Zones           = 1<<0,
		Enable_GPU_Zones           = 1<<1,  // Unreliable results on most mobile GPU.
		Enable_CPU_Clocks          = 1<<2,
		Enable_GPU_Clocks          = 1<<3,
		Enable_Thermal_Sensors     = 1<<4,
		Enable_FrameBuffer_Capture = 1<<5,  // Requires minimum of 1MB/s in network bandwidth.
		Enable_Logging             = 1<<6,
		Enable_CPU_Scheduler       = 1<<7,  // Does not work on some OS images (but fails gracefully).
		Enable_GraphicsAPI         = 1<<8,  // Requires VrApi to be integrated in the application.
		Enable_Memory              = 1<<9,  // Typically extremely high frequency!
		Enable_Head_Tracking       = 1<<10,

		// Bitfield containing features that are supported well (low overhead and reliable results)
		Default_Flags           = Enable_CPU_Zones       | Enable_CPU_Clocks          | Enable_GPU_Clocks    |
		                          Enable_Thermal_Sensors | Enable_FrameBuffer_Capture | Enable_Logging       |
		                          Enable_CPU_Scheduler   | Enable_GraphicsAPI         | Enable_Head_Tracking,

		// Bitfield containing all supported features
		All_Flags               = Enable_CPU_Zones  | Enable_GPU_Zones       | Enable_CPU_Clocks          | 
		                          Enable_GPU_Clocks | Enable_Thermal_Sensors | Enable_FrameBuffer_Capture | 
		                          Enable_Logging    | Enable_CPU_Scheduler   | Enable_GraphicsAPI         | 
		                          Enable_Memory     | Enable_Head_Tracking,
	};

	enum FrameBufferFormat
	{
		FrameBuffer_RGB_565 = 0,
		FrameBuffer_RGBA_8888,
		FrameBuffer_DXT1,
	};

	enum SensorInterpolator
	{
		Sensor_Interp_Linear = 0,
		Sensor_Interp_Nearest,
	};

	enum SensorUnits
	{
		Sensor_Unit_None = 0,

		// Frequencies...
		Sensor_Unit_Hz,
		Sensor_Unit_KHz,
		Sensor_Unit_MHz,
		Sensor_Unit_GHz,

		// Memory size...
		Sensor_Unit_Byte,
		Sensor_Unit_KByte,
		Sensor_Unit_MByte,
		Sensor_Unit_GByte,

		// Memory Bandwidth...
		Sensor_Unit_Byte_Second,
		Sensor_Unit_KByte_Second,
		Sensor_Unit_MByte_Second,
		Sensor_Unit_GByte_Second,

		// Temperature
		Sensor_Unit_Celsius,
		Sensor_Unit_Fahrenheit,
	};

	enum LogPriority
	{
		Log_Info = 0,
		Log_Warning,
		Log_Error,
	};

	// Label is basically a smart container for storing a unique identifier for string literals.
	// Take careful note, this class does not make its own copy of the name parameter! Nor does it
	// free it for you! Its optimized for literals and thus avoids mallocs, copies and frees!
	//
	// For optimal performance its also recommended you create a static const Label for each
	// string literal you want to use as as a performance zone. After the first use of the label
	// its hash will be cached into the m_hash parameter which avoids any string processing.
	// Example:
	//   static const OVR::Capture::Label foo("Foo");
	//   OVR::Capture::EnterCPUZone(foo);
	// 
	// But, if you are generating names dynamically or sourcing them from another API we also now
	// support creating temporary labels on the stack, or even using the constructor.
	// Example:
	//   const OVR::Capture::Label foo("Foo");
	//   OVR::Capture::EnterCPUZone(foo);
	// ...or...
	//   EnterCPUZone( OVR::Capture::Label("Foo") );
	// ...or...
	//   OVR::Capture::EnterCPUZone("Foo");
	//
	// ...while there is a small overhead in hashing the string on every call, we no longer send
	// redundant Labels over the network making this acceptable for low-frequency events.
	class Label
	{
		public:
			Label(const char *name) :
				m_version(0),
				m_hash(0),
				m_name(name)
			{
			}

			~Label()
			{
			}

		private:
			friend UInt32 ComputeLabelHash(const Label &label);

		private:
			mutable volatile UInt32  m_version;
			mutable volatile UInt32  m_hash;
			const char              *m_name;
	};

	template<typename T>
	struct Rect
	{
		T x;
		T y;
		T width;
		T height;

		inline bool operator==(const Rect<T> &other) const
		{
			return x==other.x && y==other.y && width==other.width && height==other.height;
		}

		inline bool operator!=(const Rect<T> &other) const
		{
			return x!=other.x || y!=other.y || width!=other.width || height!=other.height;
		}
	};

} // namespace Capture
} // namespace OVR

#endif
