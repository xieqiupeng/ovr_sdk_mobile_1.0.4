/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_Hash.cpp
Content     :   Hashing functions
Created     :   August, 2016
Notes       :
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include "OVR_Capture_Hash.h"
#include "OVR_Capture_Local.h"

#include <cstdint> // for std::uintptr_t

namespace OVR
{
namespace Capture
{

	//-----------------------------------------------------------------------------
	// MurmurHashAligned2, by Austin Appleby
	//
	// Same algorithm as MurmurHash2, but only does aligned reads - should be safer
	// on certain platforms. 
	//
	// Performance will be lower than MurmurHash2
	//
	// All code is released to the public domain. For business purposes, Murmurhash is
	// under the MIT license. 
	// https://sites.google.com/site/murmurhash/
	static UInt32 MurmurHashAligned2(const void *key, UInt32 len, UInt32 seed)
	{
		struct Utils
		{
			static inline void Mix(UInt32 &h, UInt32 &k, const UInt32 m)
			{
				const UInt32 r = 24;
				k *= m;
				k ^= k >> r;
				k *= m;
				h *= m;
				h ^= k;
			}
		};

		const UInt32 m = 0x5bd1e995;

		const UInt8 *data = reinterpret_cast<const UInt8 *>(key);

		UInt32 h = seed ^ len;

		const UInt32 align = static_cast<UInt32>( reinterpret_cast<std::uintptr_t>(data) & 3 );

		if(align && (len >= 4))
		{
			// Pre-load the temp registers

			UInt32 t = 0, d = 0;

			switch(align)
			{
				case 1: t |= data[2] << 16;
				case 2: t |= data[1] << 8;
				case 3: t |= data[0];
			}

			t <<= (8 * align);

			data += 4-align;
			len  -= 4-align;

			const UInt32 sl = 8 * (4-align);
			const UInt32 sr = 8 * align;

			// Mix

			while(len >= 4)
			{
				d = *reinterpret_cast<const UInt32*>(data);
				t = (t >> sr) | (d << sl);

				UInt32 k = t;

				Utils::Mix(h,k,m);

				t = d;

				data += 4;
				len  -= 4;
			}

			// Handle leftover data in temp registers

			d = 0;

			if(len >= align)
			{
				switch(align)
				{
					case 3: d |= data[2] << 16;
					case 2: d |= data[1] << 8;
					case 1: d |= data[0];
				}

				UInt32 k = (t >> sr) | (d << sl);
				Utils::Mix(h,k,m);

				data += align;
				len -= align;

				//----------
				// Handle tail bytes

				switch(len)
				{
					case 3: h ^= data[2] << 16;
					case 2: h ^= data[1] << 8;
					case 1: h ^= data[0];
							h *= m;
				};
			}
			else
			{
				switch(len)
				{
					case 3: d |= data[2] << 16;
					case 2: d |= data[1] << 8;
					case 1: d |= data[0];
					case 0: h ^= (t >> sr) | (d << sl);
							h *= m;
				}
			}

			h ^= h >> 13;
			h *= m;
			h ^= h >> 15;

			return h;
		}
		else
		{
			while(len >= 4)
			{
				UInt32 k = *reinterpret_cast<const UInt32*>(data);

				Utils::Mix(h,k,m);

				data += 4;
				len -= 4;
			}

			//----------
			// Handle tail bytes

			switch(len)
			{
				case 3: h ^= data[2] << 16;
				case 2: h ^= data[1] << 8;
				case 1: h ^= data[0];
						h *= m;
			};

			h ^= h >> 13;
			h *= m;
			h ^= h >> 15;

			return h;
		}
	}

	// String hash...
	UInt32 StringHash32(const char *str)
	{
		// Murmur had 0 collisions on OSX's dictionary file! Its also fast!
		// We use the aligned version as ARM doesn't support unaligned loads in all CPU modes.
		const UInt32 seed = 0;
		return MurmurHashAligned2(str, (UInt32)StringLength(str), seed);
	}

} // namespace Capture
} // namespace OVR
