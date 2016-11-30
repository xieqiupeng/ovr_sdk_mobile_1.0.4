/************************************************************************************

Filename    :   OvrApp.cpp
Content     :   Trivial use of the application framework.
Created     :   
Authors     :   

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include "OvrApp.h"
#include "GuiSys.h"
#include "OVR_Locale.h"

#if defined( OVR_OS_WIN32 )
#include "../res_pc/resource.h"
#endif

using namespace OVR;

#if defined( OVR_OS_ANDROID )
extern "C" {

jlong Java_com_yourcompany_vrtemplate_MainActivity_nativeSetAppInterface( JNIEnv * jni, jclass clazz, jobject activity,
		jstring fromPackageName, jstring commandString, jstring uriString )
{
	LOG( "nativeSetAppInterface" );
	return (new OvrTemplateApp::OvrApp())->SetActivity( jni, clazz, activity, fromPackageName, commandString, uriString );
}

} // extern "C"

#endif

namespace OvrTemplateApp
{

OvrApp::OvrApp()
	: SoundEffectContext( NULL )
	, SoundEffectPlayer( NULL )
	, GuiSys( OvrGuiSys::Create() )
	, Locale( NULL )
	, SceneModel( NULL )
{
}

OvrApp::~OvrApp()
{
	delete SoundEffectPlayer;
	SoundEffectPlayer = NULL;

	delete SoundEffectContext;
	SoundEffectContext = NULL;

	OvrGuiSys::Destroy( GuiSys );
	if ( SceneModel != NULL )
	{
		delete SceneModel;
	}
}

void OvrApp::Configure( ovrSettings & settings )
{
	settings.PerformanceParms.CpuLevel = 2;
	settings.PerformanceParms.GpuLevel = 2;
#if defined( OVR_OS_WIN32 )
	settings.WindowParms.IconResourceId = IDI_ICON1;
	settings.WindowParms.Title = "VrTemplate";		// TODO: Use VersionInfo.ProductName
#endif

	settings.RenderMode = RENDERMODE_MULTIVIEW;
}

void OvrApp::EnteredVrMode( const ovrIntentType intentType, const char * intentFromPackage, const char * intentJSON, const char * intentURI )
{
	OVR_UNUSED( intentFromPackage );
	OVR_UNUSED( intentJSON );
	OVR_UNUSED( intentURI );

	if ( intentType == INTENT_LAUNCH )
	{
		const ovrJava * java = app->GetJava();
		SoundEffectContext = new ovrSoundEffectContext( *java->Env, java->ActivityObject );
		SoundEffectContext->Initialize( &app->GetFileSys() );
		SoundEffectPlayer = new OvrGuiSys::ovrDummySoundEffectPlayer();

		Locale = ovrLocale::Create( *java->Env, java->ActivityObject, "default" );

		String fontName;
		GetLocale().GetString( "@string/font_name", "efigs.fnt", fontName );
		GuiSys->Init( this->app, *SoundEffectPlayer, fontName.ToCStr(), &app->GetDebugLines() );

		MaterialParms materialParms;
		materialParms.UseSrgbTextureFormats = false;

		const char * sceneUri = "apk:///assets/tuscany.ovrscene";

		SceneModel = LoadModelFile( app->GetFileSys(), sceneUri, Scene.GetDefaultGLPrograms(), materialParms );

		if ( SceneModel != NULL )
		{
			Scene.SetWorldModel( *SceneModel );
			Scene.SetYawOffset( -Mathf::Pi * 0.5f );
		}
		else
		{
			LOG( "OvrApp::EnteredVrMode Failed to load %s", sceneUri );
		}
	}
	else if ( intentType == INTENT_NEW )
	{
	}
}

void OvrApp::LeavingVrMode()
{
}

bool OvrApp::OnKeyEvent( const int keyCode, const int repeatCount, const KeyEventType eventType )
{
	if ( GuiSys->OnKeyEvent( keyCode, repeatCount, eventType ) )
	{
		return true;
	}
	return false;
}

ovrFrameResult OvrApp::Frame( const ovrFrameInput & vrFrame )
{
	// process input events first because this mirrors the behavior when OnKeyEvent was
	// a virtual function on VrAppInterface and was called by VrAppFramework.
	for ( int i = 0; i < vrFrame.Input.NumKeyEvents; i++ )
	{
		const int keyCode = vrFrame.Input.KeyEvents[i].KeyCode;
		const int repeatCount = vrFrame.Input.KeyEvents[i].RepeatCount;
		const KeyEventType eventType = vrFrame.Input.KeyEvents[i].EventType;

		if ( OnKeyEvent( keyCode, repeatCount, eventType ) )
		{
			continue;   // consumed the event
		}
		// If nothing consumed the key and it's a short-press of the back key, then exit the application to OculusHome.
		if ( keyCode == OVR_KEY_BACK && eventType == KEY_EVENT_SHORT_PRESS )
		{
			app->ShowSystemUI( VRAPI_SYS_UI_CONFIRM_QUIT_MENU );
			continue;
		}                
	}

	// Player movement.
	Scene.Frame( vrFrame, app->GetHeadModelParms() );

	ovrFrameResult res;
	Scene.GetFrameMatrices( vrFrame.FovX, vrFrame.FovY, res.FrameMatrices );
	Scene.GenerateFrameSurfaceList( res.FrameMatrices, res.Surfaces );

	// Update GUI systems after the app frame, but before rendering anything.
	GuiSys->Frame( vrFrame, res.FrameMatrices.CenterView );
	// Append GuiSys surfaces.
	GuiSys->AppendSurfaceList( res.FrameMatrices.CenterView, &res.Surfaces );

	FrameParms = vrapi_DefaultFrameParms( app->GetJava(), VRAPI_FRAME_INIT_DEFAULT, vrapi_GetTimeInSeconds(), NULL );
	ovrFrameLayer & worldLayer = FrameParms.Layers[0];
	for ( int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++ )
	{
		worldLayer.Textures[eye].ColorTextureSwapChain = vrFrame.ColorTextureSwapChain[eye];
		worldLayer.Textures[eye].DepthTextureSwapChain = vrFrame.DepthTextureSwapChain[eye];
		worldLayer.Textures[eye].TextureSwapChainIndex = vrFrame.TextureSwapChainIndex;

		worldLayer.Textures[eye].TexCoordsFromTanAngles = vrFrame.TexCoordsFromTanAngles;
		worldLayer.Textures[eye].HeadPose = vrFrame.Tracking.HeadPose;
	}

	FrameParms.ExternalVelocity = Scene.GetExternalVelocity();
	worldLayer.Flags = VRAPI_FRAME_LAYER_FLAG_CHROMATIC_ABERRATION_CORRECTION;

	res.FrameParms = (ovrFrameParmsExtBase *) & FrameParms;
	return res;
}

} // namespace OvrTemplateApp
