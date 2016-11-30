/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_StandardSensors.cpp
Content     :   Oculus performance capture library. Support for standard builtin device sensors.
Created     :   January, 2015
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include "OVR_Capture_StandardSensors.h"
#include "OVR_Capture_Local.h"
#include "OVR_Capture_FileIO.h"

#include <algorithm>
#include <string>
#include <sstream>
#include <iterator>

namespace OVR
{
namespace Capture
{

	// Container for label whose name is read from disk. Must be stored in
	// global memory (or local static) as name is assumed to be persistent.
	struct SensorLabelDesc
	{
		static const float     minValue;
		float                  maxValue     = 0.0f;
		SensorInterpolator     interpolator = Sensor_Interp_Linear;
		SensorUnits            units        = Sensor_Unit_None;
		
		static const size_t    nameSize     = 32;
		char                   name[nameSize];
	};
	const float SensorLabelDesc::minValue = 0.0f;

	struct ClockFileDesc
	{
		const char *currPath;
		const char *maxPath;
		SensorUnits units;
	};

	// List of files to query for memory clocks...
	static const ClockFileDesc g_memClockDescs[] =
	{
		// Snapdragon...
		{ "/sys/class/devfreq/0.qcom,cpubw/cur_freq",         "/sys/class/devfreq/0.qcom,cpubw/max_freq",         Sensor_Unit_MHz },
		{ "/sys/class/devfreq/soc:qcom,cpubw/cur_freq",       "/sys/class/devfreq/soc:qcom,cpubw/max_freq",       Sensor_Unit_MHz },
		// Exynos...
		{ "/sys/class/devfreq/exynos5-devfreq-mif/cur_freq",  "/sys/class/devfreq/exynos5-devfreq-mif/max_freq",  Sensor_Unit_KHz },
		{ "/sys/class/devfreq/exynos7-devfreq-mif/cur_freq",  "/sys/class/devfreq/exynos7-devfreq-mif/max_freq",  Sensor_Unit_KHz },
		{ "/sys/class/devfreq/17000010.devfreq_mif/cur_freq", "/sys/class/devfreq/17000010.devfreq_mif/max_freq", Sensor_Unit_KHz },
	};
	static const UInt32    g_memClockDescCount = sizeof(g_memClockDescs) / sizeof(g_memClockDescs[0]);

	static const UInt32    g_maxCpus = 8;
	static SensorLabelDesc cpuDescs[g_maxCpus];

	static SensorLabelDesc g_gpuDesc;

	static SensorLabelDesc g_memDesc;

	static const UInt32    g_maxThermalSensors = 20;
	static SensorLabelDesc g_thermalDescs[g_maxThermalSensors];


	StandardSensors::StandardSensors(void)
	{
	}

	StandardSensors::~StandardSensors(void)
	{
	}

	void StandardSensors::OnThreadExecute(void)
	{
		SetThreadName("CaptureSensors");

		// Pre-load what files we can... reduces open/close overhead (which is significant)

		// Setup CPU Clocks Support...
		FileHandle cpuOnlineFiles[g_maxCpus];
		FileHandle cpuFreqFiles[g_maxCpus];
		for(UInt32 i=0; i<g_maxCpus; i++)
		{
			cpuOnlineFiles[i] = cpuFreqFiles[i] = NullFileHandle;
		}
		if(CheckConnectionFlag(Enable_CPU_Clocks))
		{
			int maxFreq = 0;
			for(UInt32 i=0; i<g_maxCpus; i++)
			{
				char onlinePath[64] = {0};
				FormatString(onlinePath, sizeof(onlinePath), "/sys/devices/system/cpu/cpu%u/online", i);
				cpuOnlineFiles[i] = OpenFile(onlinePath);
				cpuFreqFiles[i]   = NullFileHandle;
				if(cpuOnlineFiles[i] != NullFileHandle)
				{
					char maxFreqPath[64] = {0};
					FormatString(maxFreqPath, sizeof(maxFreqPath), "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq", i);
					maxFreq = std::max(maxFreq, ReadIntFile(maxFreqPath));
				}
			}
			for(UInt32 i=0; i<g_maxCpus; i++)
			{
				SensorLabelDesc &desc = cpuDescs[i];
				desc.maxValue     = (float)maxFreq;
				desc.interpolator = Sensor_Interp_Nearest;
				desc.units        = Sensor_Unit_KHz;

				FormatString(desc.name, desc.nameSize, "CPU%u Clocks", i);
			}
		}

		// Setup GPU Clocks Support...
		FileHandle gpuFreqFile = NullFileHandle;
		if(CheckConnectionFlag(Enable_GPU_Clocks))
		{
			if(gpuFreqFile == NullFileHandle) // Adreno
			{
				gpuFreqFile = OpenFile("/sys/class/kgsl/kgsl-3d0/gpuclk");
				if(gpuFreqFile != NullFileHandle)
				{
					g_gpuDesc.maxValue     = (float)ReadIntFile("/sys/class/kgsl/kgsl-3d0/max_gpuclk");
					g_gpuDesc.interpolator = Sensor_Interp_Nearest;
					g_gpuDesc.units        = Sensor_Unit_Hz;
				}
			}
			if(gpuFreqFile == NullFileHandle) // Mali
			{
				gpuFreqFile = OpenFile("/sys/class/misc/mali0/device/clock");
				if(gpuFreqFile != NullFileHandle)
				{
					std::string buf(256, '\0');
					ReadFileLine("/sys/class/misc/mali0/device/dvfs_table", &buf[0], (int)buf.size());

					// dvfs_table contains a space delimited list of all possible clock rates... so pick the greatest...
					int maxFreq = 0;
					std::stringstream bufstream(buf);
					std::istream_iterator<int> begin(bufstream);
					std::istream_iterator<int> end;
					for(std::istream_iterator<int> s=begin; s!=end; s++)
					{
						maxFreq = std::max(maxFreq, *s);
					}
					g_gpuDesc.maxValue     = (float)maxFreq;
					g_gpuDesc.interpolator = Sensor_Interp_Nearest;
					g_gpuDesc.units        = Sensor_Unit_MHz;
				}
			}
			if(gpuFreqFile != NullFileHandle)
			{
				FormatString(g_gpuDesc.name, g_gpuDesc.nameSize, "GPU Clocks");
			}
		}

		// Setup Memory Clocks Support...
		FileHandle memFreqFile = NullFileHandle;
		if(CheckConnectionFlag(Enable_CPU_Clocks))
		{
			for(UInt32 i=0; i<g_memClockDescCount; i++)
			{
				const ClockFileDesc &desc = g_memClockDescs[i];
				memFreqFile = OpenFile(desc.currPath);
				if(memFreqFile != NullFileHandle)
				{
					g_memDesc.maxValue = (float)ReadIntFile(desc.maxPath);
					g_memDesc.units    = desc.units;
					break;
				}
			}
			if(memFreqFile != NullFileHandle)
			{
				g_memDesc.interpolator = Sensor_Interp_Nearest;
				FormatString(g_memDesc.name, g_memDesc.nameSize, "Mem Clocks");
			}
		}

		// Setup thermal sensors...
		FileHandle thermalFiles[g_maxThermalSensors];
		for(UInt32 i=0; i<g_maxThermalSensors; i++)
		{
			thermalFiles[i] = NullFileHandle;
		}
		if(CheckConnectionFlag(Enable_Thermal_Sensors))
		{
			for(UInt32 i=0; i<g_maxThermalSensors; i++)
			{
				SensorLabelDesc &desc = g_thermalDescs[i];

				char typePath[64]      = {0};
				char tempPath[64]      = {0};
				char tripPointPath[64] = {0};
				FormatString(typePath,      sizeof(typePath),      "/sys/devices/virtual/thermal/thermal_zone%u/type", i);
				FormatString(tempPath,      sizeof(tempPath),      "/sys/devices/virtual/thermal/thermal_zone%u/temp", i);
				FormatString(tripPointPath, sizeof(tripPointPath), "/sys/devices/virtual/thermal/thermal_zone%u/trip_point_0_temp", i);

				// Initialize the Label...
				if(ReadFileLine(typePath, desc.name, sizeof(desc.name)) <= 0)
					continue; // failed to read sensor name...

				char modePath[64] = {0};
				FormatString(modePath, sizeof(modePath), "/sys/devices/virtual/thermal/thermal_zone%d/mode", i);

				// check to see if the zone is disabled... its okay if there is no mode file...
				char mode[16] = {0};
				if(ReadFileLine(modePath, mode, sizeof(mode))>0 && !strcmp(mode, "disabled"))
					continue;

				// Finally... open the file.
				thermalFiles[i] = OpenFile(tempPath);

				// Check to see if the temperature file was found...
				if(thermalFiles[i] == NullFileHandle)
					continue;

				// Read in the critical temperature value.
				const int tripPoint = ReadIntFile(tripPointPath);
				if(tripPoint > 0)
				{
					desc.maxValue     = (float)tripPoint;
					desc.interpolator = Sensor_Interp_Linear;
					desc.units        = Sensor_Unit_None;
				}
			}
		}

		// For clocks, we store the last value and only send updates when it changes since we
		// use blocking chart rendering.
		int lastCpuFreq[g_maxCpus] = {};
		int lastGpuFreq            = 0;
		int lastMemValue           = 0;

		UInt32 sampleCount = 0;

		while(!QuitSignaled() && IsConnected())
		{
			// Sample CPU Frequencies...
			for(UInt32 i=0; i<g_maxCpus; i++)
			{
				// If the 'online' file can't be found, then we just assume this CPU doesn't even exist
				if(cpuOnlineFiles[i] == NullFileHandle)
					continue;

				const SensorLabelDesc &desc   = cpuDescs[i];
				const bool             online = ReadIntFile(cpuOnlineFiles[i]) ? true : false;
				if(online && cpuFreqFiles[i]==NullFileHandle)
				{
					// Open the frequency file if we are online and its not already open...
					char freqPath[64] = {0};
					FormatString(freqPath, sizeof(freqPath), "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", i);
					cpuFreqFiles[i] = OpenFile(freqPath);
				}
				else if(!online && cpuFreqFiles[i]!=NullFileHandle)
				{
					// close the frequency file if we are no longer online
					CloseFile(cpuFreqFiles[i]);
					cpuFreqFiles[i] = NullFileHandle;
				}
				const int freq = cpuFreqFiles[i]==NullFileHandle ? 0 : ReadIntFile(cpuFreqFiles[i]);
				if(freq != lastCpuFreq[i])
				{
					// Convert from KHz to Hz
					Sensor(desc.name, (float)freq, desc.minValue, desc.maxValue, desc.interpolator, desc.units);
					lastCpuFreq[i] = freq;
				}
				ThreadYield();
			}

			// Sample GPU Frequency...
			if(gpuFreqFile != NullFileHandle)
			{
				const int freq = ReadIntFile(gpuFreqFile);
				if(freq != lastGpuFreq)
				{
					Sensor(g_gpuDesc.name, (float)freq, g_gpuDesc.minValue, g_gpuDesc.maxValue, g_gpuDesc.interpolator, g_gpuDesc.units);
					lastGpuFreq = freq;
				}
			}

			// Sample Memory Bandwidth
			if(memFreqFile != NullFileHandle)
			{
				const int value = ReadIntFile(memFreqFile);
				if(value != lastMemValue)
				{
					Sensor(g_memDesc.name, (float)value, g_memDesc.minValue, g_memDesc.maxValue, g_memDesc.interpolator, g_memDesc.units);
					lastMemValue = value;
				}
			}

			// Sample thermal sensors...
			if((sampleCount&15) == 0) // sample temperature at a much lower frequency as clocks... thermals don't change that fast.
			{
				for(UInt32 i=0; i<g_maxThermalSensors; i++)
				{
					FileHandle file = thermalFiles[i];
					if(file != NullFileHandle)
					{
						const SensorLabelDesc &desc = g_thermalDescs[i];
						Sensor(desc.name, (float)ReadIntFile(file), desc.minValue, desc.maxValue, desc.interpolator, desc.units);
					}
				}
				ThreadYield();
			}

			// Sleep 5ms between samples...
			ThreadSleepMicroseconds(5000);
			sampleCount++;
		}

		// Close down cached file handles...
		for(UInt32 i=0; i<g_maxCpus; i++)
		{
			if(cpuOnlineFiles[i] != NullFileHandle) CloseFile(cpuOnlineFiles[i]);
			if(cpuFreqFiles[i]   != NullFileHandle) CloseFile(cpuFreqFiles[i]);
		}
		if(gpuFreqFile != NullFileHandle) CloseFile(gpuFreqFile);
		if(memFreqFile != NullFileHandle) CloseFile(memFreqFile);
		for(UInt32 i=0; i<g_maxThermalSensors; i++)
		{
			if(thermalFiles[i] != NullFileHandle) CloseFile(thermalFiles[i]);
		}
	}

} // namespace Capture
} // namespace OVR

