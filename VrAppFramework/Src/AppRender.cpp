/************************************************************************************

Filename    :   AppRender.cpp
Content     :
Created     :
Authors     :

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/
#include <math.h>

#include "Kernel/OVR_GlUtils.h"
#include "VrApi.h"
#include "VrApi_Helpers.h"

#include "App.h"
#include "AppLocal.h"
#include "VrCommon.h"
#include "DebugLines.h"
#include "DebugConsole.h"

//#define OVR_USE_PERF_TIMER
#include "OVR_PerfTimer.h"

namespace OVR
{
static int ClipPolygonToPlane( Vector4f * dstPoints, const Vector4f * srcPoints, int srcPointCount, int planeAxis, float planeDist, float planeDir )
{
	int sides[16] = { 0 };

	for ( int p0 = 0; p0 < srcPointCount; p0++ )
	{
		const int p1 = ( p0 + 1 ) % srcPointCount;
		sides[p0] = planeDir * srcPoints[p0][planeAxis] < planeDist * srcPoints[p0].w;

		const float d0 = srcPoints[p0].w * planeDir * planeDist - srcPoints[p0][planeAxis];
		const float d1 = srcPoints[p1].w * planeDir * planeDist - srcPoints[p1][planeAxis];
		const float delta = d0 - d1;
		const float fraction = fabsf( delta ) > Mathf::SmallestNonDenormal ? ( d0 / delta ) : 1.0f;
		const float clamped = Alg::Clamp( fraction, 0.0f, 1.0f );

		dstPoints[p0 * 2 + 0] = srcPoints[p0];
		dstPoints[p0 * 2 + 1] = srcPoints[p0] + ( srcPoints[p1] - srcPoints[p0] ) * clamped;
	}

	sides[srcPointCount] = sides[0];

	int dstPointCount = 0;
	for ( int p = 0; p < srcPointCount; p++ )
	{
		if ( sides[p + 0] != 0 )
		{
			dstPoints[dstPointCount++] = dstPoints[p * 2 + 0];
		}
		if ( sides[p + 0] != sides[p + 1] )
		{
			dstPoints[dstPointCount++] = dstPoints[p * 2 + 1];
		}
	}

	OVR_ASSERT( dstPointCount <= 16 );
	return dstPointCount;
}

static ovrRectf TextureRectForBounds( const Bounds3f & bounds, const Matrix4f & modelViewProjectionMatrix )
{
	Vector4f projectedCorners[8];
	for ( int i = 0; i < 8; i++ )
	{
		const Vector4f corner(
			bounds.b[( i ^ ( i >> 1 ) ) & 1][0],
			bounds.b[( i >> 1 ) & 1][1],
			bounds.b[( i >> 2 ) & 1][2],
			1.0f
		);
		projectedCorners[i] = modelViewProjectionMatrix.Transform( corner );
	}

	const int sideIndices[6][4] =
	{
		{ 0, 3, 7, 4 },
		{ 1, 2, 6, 5 },
		{ 0, 1, 5, 4 },
		{ 2, 3, 7, 6 },
		{ 0, 1, 2, 3 },
		{ 4, 5, 6, 7 }
	};

	Vector4f clippedCorners[6 * 16];
	int clippedCornerCount = 0;
	for ( int i = 0; i < 6; i++ )
	{
		clippedCorners[clippedCornerCount + 0] = projectedCorners[sideIndices[i][0]];
		clippedCorners[clippedCornerCount + 1] = projectedCorners[sideIndices[i][1]];
		clippedCorners[clippedCornerCount + 2] = projectedCorners[sideIndices[i][2]];
		clippedCorners[clippedCornerCount + 3] = projectedCorners[sideIndices[i][3]];
		Vector4f * corners0 = &clippedCorners[clippedCornerCount];
		Vector4f corners1[2 * 16];
		int cornerCount = 4;
		cornerCount = ClipPolygonToPlane( corners1, corners0, cornerCount, 0, 1.0f, -1.0f );
		cornerCount = ClipPolygonToPlane( corners0, corners1, cornerCount, 0, 1.0f, +1.0f );
		cornerCount = ClipPolygonToPlane( corners1, corners0, cornerCount, 1, 1.0f, -1.0f );
		cornerCount = ClipPolygonToPlane( corners0, corners1, cornerCount, 1, 1.0f, +1.0f );
		cornerCount = ClipPolygonToPlane( corners1, corners0, cornerCount, 2, 1.0f, -1.0f );
		cornerCount = ClipPolygonToPlane( corners0, corners1, cornerCount, 2, 1.0f, +1.0f );
		clippedCornerCount += cornerCount;
	}

	if ( clippedCornerCount == 0 )
	{
		const ovrRectf rect = { 0.0f, 0.0f, 0.0f, 0.0f };
		return rect;
	}

	Bounds3f clippedBounds( Bounds3f::Init );
	for ( int i = 0; i < clippedCornerCount; i++ )
	{
		OVR_ASSERT( clippedCorners[i].w > Mathf::SmallestNonDenormal );
		const float recip = 0.5f / clippedCorners[i].w;
		const Vector3f point(
			clippedCorners[i].x * recip + 0.5f,
			clippedCorners[i].y * recip + 0.5f,
			clippedCorners[i].z * recip + 0.5f );
		clippedBounds.AddPoint( point );
	}

	for ( int i = 0; i < 3; i++ )
	{
		clippedBounds.GetMins()[i] = Alg::Clamp( clippedBounds.GetMins()[i], 0.0f, 1.0f );
		clippedBounds.GetMaxs()[i] = Alg::Clamp( clippedBounds.GetMaxs()[i], 0.0f, 1.0f );
	}

	ovrRectf rect;
	rect.x = clippedBounds.GetMins().x;
	rect.y = clippedBounds.GetMins().y;
	rect.width = clippedBounds.GetMaxs().x - clippedBounds.GetMins().x;
	rect.height = clippedBounds.GetMaxs().y - clippedBounds.GetMins().y;

	return rect;
}

static Bounds3f BoundsForSurfaceList( const OVR::Array< ovrDrawSurface > & surfaceList )
{
	Bounds3f surfaceBounds( Bounds3f::Init );
	for ( int i = 0; i < surfaceList.GetSizeI(); i++ )
	{
		const ovrDrawSurface * surf = &surfaceList[i];
		if ( surf == NULL || surf->surface == NULL )
		{
			continue;
		}

		const Vector3f size = surf->surface->geo.localBounds.GetSize();
		if ( size.x == 0.0f && size.y == 0.0f && size.z == 0.0f )
		{
			WARN( "surface[%s]->cullingBounds = (%f %f %f)-(%f %f %f)", surf->surface->surfaceName.ToCStr(),
				surf->surface->geo.localBounds.GetMins().x, surf->surface->geo.localBounds.GetMins().y, surf->surface->geo.localBounds.GetMins().z,
				surf->surface->geo.localBounds.GetMaxs().x, surf->surface->geo.localBounds.GetMaxs().y, surf->surface->geo.localBounds.GetMaxs().z );
			OVR_ASSERT( false );
			continue;
		}
		const Bounds3f worldBounds = Bounds3f::Transform( surf->modelMatrix, surf->surface->geo.localBounds );
		surfaceBounds = Bounds3f::Union( surfaceBounds, worldBounds );
	}

	return surfaceBounds;
}

void AppLocal::DrawEyeViews( ovrFrameResult & res )
{
	OVR_PERF_TIMER( AppLocal_DrawEyeViews );

	// Flush out and report any errors
	GL_CheckErrors( "FrameStart" );

	EyeBuffers->BeginFrame();

	// Increase the fov by about 10 degrees if we are not holding 60 fps so
	// there is less black pull-in at the edges.
	//
	// Doing this dynamically based just on time causes visible flickering at the
	// periphery when the fov is increased, so only do it if minimumVsyncs is set.
	const float fovIncrease = ( ( VrSettings.MinimumVsyncs > 1 ) || TheVrFrame.Get().DeviceStatus.PowerLevelStateThrottled ) ? 10.0f : 0.0f;
	const float fovDegreesX = SuggestedEyeFovDegreesX + fovIncrease;
	const float fovDegreesY = SuggestedEyeFovDegreesY + fovIncrease;

	// Setup the default frame parms first so they can be modified in DrawEyeView().
	const ovrFrameTextureSwapChains eyes = EyeBuffers->GetCurrentFrameTextureSwapChains();
	const ovrMatrix4f texCoordsFromTanAngles = ovrMatrix4f_TanAngleMatrixFromProjection( (ovrMatrix4f*)( &res.FrameMatrices.EyeProjection[ 0 ] ) );

	// ----TODO_DRAWEYEVIEW - remove once DrawEyeView path is removed as apps should be supplying the
	// FrameParms in FrameResult.
	ovrFrameParms fallbackFrameParms;
	if ( res.FrameParms == NULL )
	{
		fallbackFrameParms = vrapi_DefaultFrameParms( GetJava(), VRAPI_FRAME_INIT_DEFAULT, vrapi_GetTimeInSeconds(), NULL );
		res.FrameParms = (ovrFrameParmsExtBase *) & fallbackFrameParms;
	}
	// ----TODO_DRAWEYEVIEW

	ovrFrameParms & frameParms = *vrapi_GetFrameParms( res.FrameParms );

	const bool renderMonoMode = ( VrSettings.RenderMode & RENDERMODE_TYPE_MONO_MASK ) != 0;

	// ----TODO_DRAWEYEVIEW - remove once DrawEyeView path is removed.
	static int frameCount = 0;
	frameCount++;
	const bool useFrameResultSurfaces = ( ( VrSettings.RenderMode & RENDERMODE_TYPE_FRAME_SURFACES_MASK ) != 0 ) ||
									  ( ( ( VrSettings.RenderMode & RENDERMODE_TYPE_DEBUG_MASK ) != 0 ) && ( ( frameCount & 0x8 ) > 0 ) );

	if ( !useFrameResultSurfaces )
	{
		frameParms = vrapi_DefaultFrameParms( GetJava(), VRAPI_FRAME_INIT_DEFAULT, vrapi_GetTimeInSeconds(), NULL );
		ovrFrameLayer & worldLayer = frameParms.Layers[0];
		for ( int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++ )
		{
			worldLayer.Textures[eye].ColorTextureSwapChain = eyes.ColorTextureSwapChain[renderMonoMode ? 0 : eye];
			worldLayer.Textures[eye].DepthTextureSwapChain = eyes.DepthTextureSwapChain[renderMonoMode ? 0 : eye];
			worldLayer.Textures[eye].TextureSwapChainIndex = eyes.TextureSwapChainIndex;
			for ( int layer = 0; layer < VRAPI_FRAME_LAYER_TYPE_MAX; layer++ )
			{
				frameParms.Layers[layer].Textures[eye].TexCoordsFromTanAngles = texCoordsFromTanAngles;
				frameParms.Layers[layer].Textures[eye].HeadPose = TheVrFrame.Get().Tracking.HeadPose;
			}
		}
	}
	// ----TODO_DRAWEYEVIEW - remove once DrawEyeView path is removed.

	// If TexRectLayer specified, compute a texRect which covers the render surface list for the layer.
	if ( useFrameResultSurfaces && res.TexRectLayer >= 0 && res.TexRectLayer < VRAPI_FRAME_LAYER_TYPE_MAX )
	{
		const Bounds3f surfaceBounds = BoundsForSurfaceList( res.Surfaces );
		ovrFrameLayer & layer = frameParms.Layers[res.TexRectLayer];
		for ( int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++ )
		{
			layer.Textures[eye].TextureRect = TextureRectForBounds( surfaceBounds, res.FrameMatrices.EyeProjection[eye] * res.FrameMatrices.EyeView[eye] );
		}
	}

	// ----TODO_DRAWEYEVIEW
	// Once all apps are using the frame result surfaces, all of this #if should be removed and
	// debug lines and debug font surfaces should be added to the frame result Surfaces array
	// from VrThreadFunction. We cannot add them to that list now because it will trigger use
	// of the frame result surface for apps that are not yet submitting surfaces in the frame results.
	// This at least allows the frame result surfaces path to only call RenderSurfaceList once
	// per eye instead of 3 times per eye.
	Array< ovrDrawSurface > frameworkSurfaces;
	if ( useFrameResultSurfaces )
	{
		GetDebugLines().AppendSurfaceList( res.Surfaces );
	}
	else
	{
		GetDebugLines().AppendSurfaceList( frameworkSurfaces );
	}
	// ----TODO_DRAWEYEVIEW

	// ----TODO_DRAWEYEVIEW -- we should not be over-writing these parms here as
	// we may be destroying any data the app has set.
	frameParms.FrameIndex = TheVrFrame.Get().FrameNumber;
	frameParms.MinimumVsyncs = VrSettings.MinimumVsyncs;
	frameParms.PerformanceParms = VrSettings.PerformanceParms;
	// ----TODO_DRAWEYEVIEW

	// DisplayMonoMode uses a single eye rendering for speed improvement
	// and / or high refresh rate double-scan hardware modes.
	const int numPasses = ( renderMonoMode || UseMultiview ) ? 1 : 2;

	// Render each eye.
	OVR_PERF_TIMER( AppLocal_DrawEyeViews_RenderEyes );
	for ( int eye = 0; eye < numPasses; eye++ )
	{
		EyeBuffers->BeginRenderingEye( eye );

		// Call back to the app for drawing.
		if ( useFrameResultSurfaces )
		{
			if ( res.ClearDepthBuffer && res.ClearColorBuffer )
			{
				glClearColor( res.ClearColor.x, res.ClearColor.y, res.ClearColor.z, res.ClearColor.w );
				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
			}
			else if ( res.ClearColorBuffer )
			{
				//--- MV_INVALIDATE_WORKAROUND
				// On the Exynos 8890 with multiview enabled, we get rendering corruption in view 0
				// when issuing an invalidate or clear at the start of the frame. As a workaround,
				// we render a quad with the clear color. This likely introduces extra timing into
				// the system which happens to fix the issue for our use case.
				if ( UseMultiview )
				{
					ScreenClearColor = res.ClearColor;
					res.Surfaces.InsertAt( 0, ovrDrawSurface( &ScreenClearSurf ) );
				}
				//--- MV_INVALIDATE_WORKAROUND
				else
				{
					glClearColor( res.ClearColor.x, res.ClearColor.y, res.ClearColor.z, res.ClearColor.w );
					glClear( GL_COLOR_BUFFER_BIT );
				}
			}
			else if ( res.ClearDepthBuffer )
			{
				glClear( GL_DEPTH_BUFFER_BIT );
			}

			// Render the surfaces returned by Frame.
			SurfaceRender.RenderSurfaceList( res.Surfaces, res.FrameMatrices.EyeView[ 0 ], res.FrameMatrices.EyeProjection[ 0 ], eye );
		}
		else
		{
			appInterface->DrawEyeView( eye, fovDegreesX, fovDegreesY, frameParms );
			SurfaceRender.RenderSurfaceList( frameworkSurfaces, res.FrameMatrices.EyeView[ eye ], res.FrameMatrices.EyeProjection[ eye ] );
		}

		glDisable( GL_DEPTH_TEST );
		glDisable( GL_CULL_FACE );

		// Draw a thin vignette at the edges of the view so clamping will give black
		// This will not be reflected correctly in overlay planes.
		if ( extensionsOpenGL.EXT_texture_border_clamp == false )
		{
			OVR_PERF_TIMER( AppLocal_DrawEyeViews_FillEdge );
			{
				// We need destination alpha to be solid 1 at the edges to prevent overlay
				// plane rendering from bleeding past the rendered view border, but we do
				// not want to fade to that, which would cause overlays to fade out differently
				// than scene geometry.

				// We could skip this for the cube map overlays when used for panoramic photo viewing
				// with no foreground geometry to get a little more fov effect, but if there
				// is a swipe view or anything else being rendered, the border needs to
				// be respected.

				// Note that this single pixel border won't be sufficient if mipmaps are made for
				// the eye buffers.

				// Probably should do this with GL_LINES instead of scissor changing.
				const int fbWidth = VrSettings.EyeBufferParms.resolutionWidth;
				const int fbHeight = VrSettings.EyeBufferParms.resolutionHeight;
				glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
				glEnable( GL_SCISSOR_TEST );

				glScissor( 0, 0, fbWidth, 1 );
				glClear( GL_COLOR_BUFFER_BIT );

				glScissor( 0, fbHeight-1, fbWidth, 1 );
				glClear( GL_COLOR_BUFFER_BIT );

				glScissor( 0, 0, 1, fbHeight );
				glClear( GL_COLOR_BUFFER_BIT );

				glScissor( fbWidth-1, 0, 1, fbHeight );
				glClear( GL_COLOR_BUFFER_BIT );

				glScissor( 0, 0, fbWidth, fbHeight );
				glDisable( GL_SCISSOR_TEST );
			}
		}

		{
			OVR_PERF_TIMER( AppLocal_DrawEyeViews_EndRenderingEye );
			EyeBuffers->EndRenderingEye( eye );
		}
	}

	OVR_PERF_TIMER_STOP( AppLocal_DrawEyeViews_RenderEyes );

	EyeBuffers->EndFrame();

	OVR_PERF_TIMER_STOP( AppLocal_DrawEyeViews );

	{
		OVR_PERF_TIMER( AppLocal_DrawEyeViews_SubmitFrame );
		vrapi_SubmitFrame( OvrMobile, (ovrFrameParms *) res.FrameParms );
	}
}

}	// namespace OVR
