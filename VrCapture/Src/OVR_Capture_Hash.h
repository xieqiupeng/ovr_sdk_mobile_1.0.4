/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_Hash.h
Content     :   Hashing functions
Created     :   August, 2016
Notes       :
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#ifndef OVR_CAPTURE_HASH_H
#define OVR_CAPTURE_HASH_H

#include <OVR_Capture_Config.h>
#include <OVR_Capture_Types.h>

namespace OVR
{
namespace Capture
{

	// String hash...
	UInt32 StringHash32(const char *str);

} // namespace Capture
} // namespace OVR

#endif
