/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_PerfEvent.h
Content     :   Oculus performance capture library. Support for linux kernel perf events
Created     :   March, 2016
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#ifndef OVR_CAPTURE_PERFEVENT_H
#define OVR_CAPTURE_PERFEVENT_H

#include <OVR_Capture_Config.h>

#if defined(OVR_CAPTURE_HAS_PERF)

#include <OVR_Capture.h>
#include "OVR_Capture_Thread.h"

#include <unordered_map>

namespace OVR
{
namespace Capture
{

	class PerfEvent : public Thread
	{
		public:
			PerfEvent(void);
			virtual ~PerfEvent(void);

			void Start(void);

			void AttachAllTasks(void);

		private:
			struct PerfTask
			{
				int    fd        = 0;
				void  *mapped    = nullptr;
				UInt64 totalTime = 0;
			};

			typedef std::unordered_map<pid_t,PerfTask> PerfTaskMap;

		private:
			virtual void OnThreadExecute(void);

			void AttachTask(pid_t tid);
			void DetachTask(pid_t tid);
			void DetachTask(PerfTaskMap::iterator task);

		private:
			static const bool s_verbose            = false;
			static const bool s_retainFileHandle   = false;
			UInt64            m_threadCreationTime = 0;
			PerfTaskMap       m_tasks;
			ThreadGate        m_gate;
	};

} // namespace Capture
} // namespace OVR

#endif // #if defined(OVR_CAPTURE_ANDROID)
#endif
