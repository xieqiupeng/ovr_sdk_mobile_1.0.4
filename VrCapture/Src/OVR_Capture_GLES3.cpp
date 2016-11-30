/************************************************************************************

PublicHeader:   OVR_Capture.h
Filename    :   OVR_Capture_GLES3.cpp
Content     :   Oculus performance capture library. OpenGL ES 3 interfaces.
Created     :   January, 2015
Notes       : 
Author      :   James Dolan

Copyright   :   Copyright 2015 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include <OVR_Capture_GLES3.h>

#if defined(OVR_CAPTURE_HAS_GLES3)

#include <OVR_Capture_Packets.h>
#include "OVR_Capture_Local.h"
#include "OVR_Capture_AsyncStream.h"

#include <deque>

#include <EGL/egl.h>

#if defined(OVR_CAPTURE_HAS_OPENGL_LOADER)
	#include <GLES3/gl3_loader.h>
	#include <GLES2/gl2ext_loader.h>
	using namespace GLES3;
	using namespace GLES2EXT;
#else
	#include <GLES3/gl3.h>
#endif

// Experimental DXT1 encoding of framebuffer on the GPU to reduce network bandwidth
#define OVR_CAPTURE_GPU_DXT1_ENCODE

namespace OVR
{
namespace Capture
{

	struct PendingFrameBuffer
	{
		UInt64 timestamp;
		GLuint renderbuffer;
		GLuint fbo;
		GLuint pbo;
		bool   imageReady;
	};

	static bool                    g_requireCleanup         = false;

	static const UInt32            g_maxPendingFramebuffers = 3;
	static UInt32                  g_nextPendingFramebuffer = 0;
	static PendingFrameBuffer      g_pendingFramebuffers[g_maxPendingFramebuffers];

#if defined(OVR_CAPTURE_GPU_DXT1_ENCODE) // 4bpp... 192x192@60 = 1.05MB/s
	static const UInt32            g_imageWidth        = 192;
	static const UInt32            g_imageHeight       = 192;
	static const UInt32            g_blockByteSize     = 8;
	static const UInt32            g_imageWidthBlocks  = g_imageWidth>>2;
	static const UInt32            g_imageHeightBlocks = g_imageHeight>>2;
	static const FrameBufferFormat g_imageFormat       = FrameBuffer_DXT1;
	static const GLenum            g_imageFormatGL     = GL_RGBA16UI;
#elif 1 // 16bpp... 128x128@60 = 1.875MB/s
	static const UInt32            g_imageWidth        = 128;
	static const UInt32            g_imageHeight       = 128;
	static const UInt32            g_blockByteSize     = 2;
	static const UInt32            g_imageWidthBlocks  = g_imageWidth;
	static const UInt32            g_imageHeightBlocks = g_imageHeight;
	static const FrameBufferFormat g_imageFormat       = FrameBuffer_RGB_565;
	static const GLenum            g_imageFormatGL     = GL_RGB565;
#else // 32bpp... 128x128@60 = 3.75MB/s... you probably shouldn't use this path outside of testing.
	static const UInt32            g_imageWidth        = 128;
	static const UInt32            g_imageHeight       = 128;
	static const UInt32            g_blockByteSize     = 4;
	static const UInt32            g_imageWidthBlocks  = g_imageWidth;
	static const UInt32            g_imageHeightBlocks = g_imageHeight;
	static const FrameBufferFormat g_imageFormat       = FrameBuffer_RGBA_8888;
	static const GLenum            g_imageFormatGL     = GL_RGBA8;
#endif
	static const UInt32            g_imageSize         = g_imageWidthBlocks * g_imageHeightBlocks * g_blockByteSize;

	static const UInt32            g_maxTimerQueries   = 32;

	static GLuint                  g_program2D      = 0; // for GL_TEXTURE_2D
	static GLuint                  g_programArray   = 0; // for GL_TEXTURE_2D_ARRAY

	static GLuint                  g_vertexArrayObject = 0;

	static bool                    g_initialized                 = false;
	static bool                    g_invalidateFramebuffers      = true;
	static bool                    g_invalidateTimerQueries      = true;
	static bool                    g_has_EXTdisjoint_timer_query = false;

	enum ShaderAttributes
	{
		POSITION_ATTRIBUTE = 0,
		TEXCOORD_ATTRIBUTE,
	};

	// Hard coded in the shaders using layout qualifiers... allows us to have multiple permutations of the shader
	// use the same uniform locations without creating any sort of crazy mapping.
	// See: g_uniformShaderSource
	enum class ShaderUniformLocations : GLuint
	{
		Texture0     = 0,
		TextureRect  = 1,
		TexelSize    = 2,
		UVBlockScale = 3,
	};

	// These must match ShaderUniformLocations!
	static const char *g_uniformShaderSource = R"=====(
		#if defined(TEXTURE_TYPE_2D_ARRAY)
			layout(location=0) uniform mediump sampler2DArray Texture0;
		#elif defined(TEXTURE_TYPE_2D)
			layout(location=0) uniform mediump sampler2D      Texture0;
		#endif

		layout(location=1) uniform mediump vec4 TextureRect;
		layout(location=2) uniform mediump vec2 TexelSize;
		layout(location=3) uniform mediump vec2 UVBlockScale;
		)=====";

	static const char *g_vertexShaderSource = R"=====(
		precision mediump float;

		in  vec4 Position;
		in  vec2 TexCoord;
		out vec2 oTexCoord;

		void main()
		{
			gl_Position = Position;
			vec2 uv = TexCoord.xy*TextureRect.zw + TextureRect.xy;
			oTexCoord = vec2(uv.x, 1.0 - uv.y);    // need to flip Y
		}
		)=====";

	static const char *g_fragmentShaderSource = R"=====(
		precision mediump float;

		in  vec2 oTexCoord;
		out vec4 Output;

		void main()
		{
		#if defined(TEXTURE_TYPE_2D_ARRAY)
			Output = texture(Texture0, vec3(oTexCoord, 0.0));
		#else
			Output = texture(Texture0, oTexCoord);
		#endif
		}
		)=====";

	static const char *g_vertexShaderSourceDXT1 = R"=====(
		precision mediump float;

		in  vec4 Position;
		in  vec2 TexCoord;
		out vec2 oTexCoord;

		void main()
		{
			gl_Position = Position;
			oTexCoord = TexCoord.xy * UVBlockScale.xy;  // don't flip Y here, we do it after applying block offsets. but clip to beginning of last block
		}
		)=====";

	// Based on http://www.nvidia.com/object/real-time-ycocg-dxt-compression.html
	static const char *g_fragmentShaderSourceDXT1 = R"=====(
		precision mediump float;
		precision mediump int;


		in  vec2  oTexCoord;
		out uvec4 Output;

		// Convert to 565 and expand back into color
		uint Encode565(inout vec3 color)
		{
			uvec3 c    = uvec3(round(color * vec3(31.0, 63.0, 31.0)));
			uint  c565 = (c.r << 11) | (c.g << 5) | c.b;
			c.rb  = (c.rb << 3) | (c.rb >> 2);
			c.g   = (c.g << 2) | (c.g >> 4);
			color = vec3(c) * (1.0 / 255.0);
			return c565;
		}

		float ColorDistance(vec3 c0, vec3 c1)
		{
			vec3 d = c0-c1;
			return dot(d, d);
		}

		void main()
		{
			vec3 block[16];

			// Load block colors...
			for(int i=0; i<4; i++)
			{
				for(int j=0; j<4; j++)
				{
					vec2 uv = (oTexCoord.xy + vec2(j,i)*TexelSize);
					uv = uv * TextureRect.zw + TextureRect.xy; // clip to TextureRect
					uv.y = 1.0 - uv.y; // flip Y
				#if defined(TEXTURE_TYPE_2D_ARRAY)
					block[i*4+j] = texture(Texture0, vec3(uv, 0.0)).rgb;
				#else
					block[i*4+j] = texture(Texture0, uv).rgb;
				#endif
				}
			}

			// Calculate bounding box...
			vec3 minblock = block[0];
			vec3 maxblock = block[0];
			for(int i=1; i<16; i++)
			{
				minblock = min(minblock, block[i]);
				maxblock = max(maxblock, block[i]);
			}

			// Inset bounding box...
			vec3 inset = (maxblock - minblock) / 16.0 - (8.0 / 255.0) / 16.0;
			minblock = clamp(minblock + inset, 0.0, 1.0);
			maxblock = clamp(maxblock - inset, 0.0, 1.0);

			// Convert to 565 colors...
			uint c0 = Encode565(maxblock);
			uint c1 = Encode565(minblock);

			// Make sure c0 has the largest integer value...
			if(c1>c0)
			{
				uint uitmp=c0; c0=c1; c1=uitmp;
				vec3 v3tmp=maxblock; maxblock=minblock; minblock=v3tmp;
			}

			// Calculate indices...
			vec3 color0 = maxblock;
			vec3 color1 = minblock;
			vec3 color2 = (color0 + color0 + color1) * (1.0/3.0);
			vec3 color3 = (color0 + color1 + color1) * (1.0/3.0);

			uint i0 = 0U;
			uint i1 = 0U;
			for(int i=0; i<8; i++)
			{
				vec3 color = block[i];
				vec4 dist;
				dist.x = ColorDistance(color, color0);
				dist.y = ColorDistance(color, color1);
				dist.z = ColorDistance(color, color2);
				dist.w = ColorDistance(color, color3);
				uvec4 b = uvec4(greaterThan(dist.xyxy, dist.wzzw));
				uint b4 = dist.z > dist.w ? 1U : 0U;
				uint index = (b.x & b4) | (((b.y & b.z) | (b.x & b.w)) << 1);
				i0 |= index << (i*2);
			}
			for(int i=0; i<8; i++)
			{
				vec3 color = block[i+8];
				vec4 dist;
				dist.x = ColorDistance(color, color0);
				dist.y = ColorDistance(color, color1);
				dist.z = ColorDistance(color, color2);
				dist.w = ColorDistance(color, color3);
				uvec4 b = uvec4(greaterThan(dist.xyxy, dist.wzzw));
				uint b4 = dist.z > dist.w ? 1U : 0U;
				uint index = (b.x & b4) | (((b.y & b.z) | (b.x & b.w)) << 1);
				i1 |= index << (i*2);
			}

			// Write out final dxt1 block...
			Output = uvec4(c0, c1, i0, i1);
		}
		)=====";
	
	static const float g_vertices[] =
	{
	   -1.0f,-1.0f, 0.0f, 1.0f,  0.0f, 0.0f,
		3.0f,-1.0f, 0.0f, 1.0f,  2.0f, 0.0f,
	   -1.0f, 3.0f, 0.0f, 1.0f,  0.0f, 2.0f,
	};

	class GLES3ScopedState
	{
		public:
			enum State
			{
				DEPTH_TEST,
				SCISSOR_TEST,
				STENCIL_TEST,
				RASTERIZER_DISCARD,
				DITHER,
				CULL_FACE,
				BLEND,

				NUM_STATES
			};
			enum TextureUnit
			{
				TEXTURE0,

				NUM_TEXTURE_UNITS
			};
			enum TextureTarget
			{
				TEXTURE_2D,
				TEXTURE_2D_ARRAY,

				NUM_TEXTURE_TARGETS
			};
		public:
			GLES3ScopedState(void)
			{
				OVR_CAPTURE_CPU_ZONE(SaveState);

				MemorySet(&m_previousViewport, 0, sizeof(m_previousViewport));
				glGetIntegerv(GL_VIEWPORT, &m_previousViewport.x);
				m_currentViewport = m_previousViewport;

				m_previousRBO = 0;
				glGetIntegerv(GL_RENDERBUFFER_BINDING, reinterpret_cast<GLint*>(&m_previousRBO));
				m_currentRBO = m_previousRBO;

				m_previousFBO = 0;
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&m_previousFBO));
				m_currentFBO = m_previousFBO;

				m_previousPBO = 0;
				glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, reinterpret_cast<GLint*>(&m_previousPBO));
				m_currentPBO = m_previousPBO;

				m_previousVAO = 0;
				glGetIntegerv(GL_VERTEX_ARRAY_BINDING, reinterpret_cast<GLint*>(&m_previousVAO));
				m_currentVAO = m_previousVAO;

				m_previousProgram = 0;
				glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&m_previousProgram));
				m_currentProgram = m_previousProgram;

				m_previousActiveTexture = GL_TEXTURE0;
				glGetIntegerv(GL_ACTIVE_TEXTURE, reinterpret_cast<GLint*>(&m_previousActiveTexture));
				m_currentActiveTexture = m_previousActiveTexture;

				for(GLuint u=0; u<NUM_TEXTURE_UNITS; u++)
				{
					ActiveTexture(GL_TEXTURE0 + u);
					for(GLuint t=0; t<NUM_TEXTURE_TARGETS; t++)
					{
						const GLuint binding = GetGlTextureTargetBinding((TextureTarget)t);
						m_previousBoundTexture[u][t] = 0;
						glGetIntegerv(binding, reinterpret_cast<GLint*>(&m_previousBoundTexture[u][t]));
						m_currentBoundTexture[u][t] = m_previousBoundTexture[u][t];
					}
				}

				MemorySet(m_previousColorMask, 0, sizeof(m_previousColorMask));
				glGetBooleanv(GL_COLOR_WRITEMASK, m_previousColorMask);
				MemoryCopy(m_currentColorMask, m_previousColorMask, sizeof(m_previousColorMask));

				MemorySet(m_stateEnums, 0, sizeof(m_stateEnums));
				m_stateEnums[DEPTH_TEST]         = GL_DEPTH_TEST;
				m_stateEnums[SCISSOR_TEST]       = GL_SCISSOR_TEST;
				m_stateEnums[STENCIL_TEST]       = GL_STENCIL_TEST;
				m_stateEnums[RASTERIZER_DISCARD] = GL_RASTERIZER_DISCARD;
				m_stateEnums[DITHER]             = GL_DITHER;
				m_stateEnums[CULL_FACE]          = GL_CULL_FACE;
				m_stateEnums[BLEND]              = GL_BLEND;
				for(GLuint i=0; i<NUM_STATES; i++)
				{
					OVR_CAPTURE_ASSERT(m_stateEnums[i] != 0); // make sure the mapping is complete
					m_previousStates[i] = glIsEnabled(m_stateEnums[i]);
					m_currentStates[i] = m_previousStates[i];
				}
			}

			~GLES3ScopedState(void)
			{
				OVR_CAPTURE_CPU_ZONE(RestoreState);

				Viewport(m_previousViewport.x, m_previousViewport.y, m_previousViewport.width, m_previousViewport.height);
				BindRenderbuffer(m_previousRBO);
				BindFramebuffer(m_previousFBO);
				BindPixelBuffer(m_previousPBO);
				BindVertexArray(m_previousVAO);

				UseProgram(m_previousProgram);

				for(GLuint u=0; u<NUM_TEXTURE_UNITS; u++)
				{
					for(GLuint t=0; t<NUM_TEXTURE_TARGETS; t++)
					{
						BindTexture((TextureUnit)u, (TextureTarget)t, m_previousBoundTexture[u][t]);
					}
				}
				ActiveTexture(m_previousActiveTexture);

				ColorMask(m_previousColorMask[0], m_previousColorMask[1], m_previousColorMask[2], m_previousColorMask[3]);

				for(GLuint i=0; i<NUM_STATES; i++)
				{
					SetState((State)i, m_previousStates[i]);
				}
			}

			void Viewport(const Rect<GLint> &viewport)
			{
				if(m_currentViewport != viewport)
				{
					glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
					m_currentViewport = viewport;
				}
			}

			void Viewport(GLint x, GLint y, GLsizei width, GLsizei height)
			{
				const Rect<GLint> viewport = {x, y, width, height};
				Viewport(viewport);
			}

			void BindRenderbuffer(GLuint rbo)
			{
				if(m_currentRBO != rbo)
				{
					glBindRenderbuffer(GL_RENDERBUFFER, rbo);
					m_currentRBO = rbo;
				}
			}

			void BindFramebuffer(GLuint fbo)
			{
				if(m_currentFBO != fbo)
				{
					glBindFramebuffer(GL_FRAMEBUFFER, fbo);
					m_currentFBO = fbo;
				}
			}

			void BindPixelBuffer(GLuint pbo)
			{
				if(m_currentPBO != pbo)
				{
					glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
					m_currentPBO = pbo;
				}
			}

			void BindVertexArray(GLuint vao)
			{
				if(m_currentVAO != vao)
				{
					glBindVertexArray(vao);
					m_currentVAO = vao;
				}
			}

			void UseProgram(GLuint program)
			{
				if(m_currentProgram != program)
				{
					glUseProgram(program);
					m_currentProgram = program;
				}
			}

			void Enable(State state)
			{
				SetState(state, GL_TRUE);
			}

			void Disable(State state)
			{
				SetState(state, GL_FALSE);
			}

			void SetState(State state, GLboolean value)
			{
				OVR_CAPTURE_ASSERT(state >= 0 && state < NUM_STATES);
				if(m_currentStates[state] != value)
				{
					if(value) glEnable( m_stateEnums[state]);
					else      glDisable(m_stateEnums[state]);
					m_currentStates[state] = value;
				}
			}

			void ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
			{
				if(m_currentColorMask[0]!=r || m_currentColorMask[1]!=g || m_currentColorMask[2]!=b || m_currentColorMask[3]!=a)
				{
					glColorMask(r, g, b, a);
					m_currentColorMask[0] = r;
					m_currentColorMask[1] = g;
					m_currentColorMask[2] = b;
					m_currentColorMask[3] = a;
				}
			}

			void BindTexture(TextureUnit textureUnit, TextureTarget target, GLuint textureid)
			{
				if(m_currentBoundTexture[textureUnit][target] != textureid)
				{
					ActiveTexture(textureUnit + GL_TEXTURE0);
					glBindTexture(GetGlTextureTarget(target), textureid);
					m_currentBoundTexture[textureUnit][target] = textureid;
				}
			}

			void BindTexture(TextureUnit textureUnit, GLuint gltarget, GLuint textureid)
			{
				BindTexture(textureUnit, GetTextureTarget(gltarget), textureid);
			}

		private:
			void ActiveTexture(GLenum textureUnit)
			{
				if(m_currentActiveTexture != textureUnit)
				{
					glActiveTexture(textureUnit);
					m_currentActiveTexture = textureUnit;
				}
			}

			TextureTarget GetTextureTarget(GLuint gltarget) const
			{
				switch(gltarget)
				{
					case GL_TEXTURE_2D:       return TEXTURE_2D;
					case GL_TEXTURE_2D_ARRAY: return TEXTURE_2D_ARRAY;
				}
				return NUM_TEXTURE_TARGETS;
			}

			GLuint GetGlTextureTarget(TextureTarget target) const
			{
				switch(target)
				{
					case TEXTURE_2D:          return GL_TEXTURE_2D;
					case TEXTURE_2D_ARRAY:    return GL_TEXTURE_2D_ARRAY;
					case NUM_TEXTURE_TARGETS: return 0;
				}
			}

			GLuint GetGlTextureTargetBinding(TextureTarget target) const
			{
				switch(target)
				{
					case TEXTURE_2D:       return GL_TEXTURE_BINDING_2D;
					case TEXTURE_2D_ARRAY: return GL_TEXTURE_BINDING_2D_ARRAY;
					case NUM_TEXTURE_TARGETS: return 0;
				}
			}

		private:
			Rect<GLint> m_previousViewport;
			Rect<GLint> m_currentViewport;

			GLuint      m_previousRBO;
			GLuint      m_currentRBO;

			GLuint      m_previousFBO;
			GLuint      m_currentFBO;

			GLuint      m_previousPBO;
			GLuint      m_currentPBO;

			GLuint      m_previousVAO;
			GLuint      m_currentVAO;

			GLuint      m_previousProgram;
			GLuint      m_currentProgram;

			GLenum      m_previousActiveTexture;
			GLenum      m_currentActiveTexture;

			GLuint      m_previousBoundTexture[NUM_TEXTURE_UNITS][NUM_TEXTURE_TARGETS];
			GLuint      m_currentBoundTexture[NUM_TEXTURE_UNITS][NUM_TEXTURE_TARGETS];

			GLboolean   m_previousColorMask[4];
			GLboolean   m_currentColorMask[4];

			GLenum      m_stateEnums[NUM_STATES];
			GLboolean   m_previousStates[NUM_STATES];
			GLboolean   m_currentStates[NUM_STATES];
	};

	static void ConditionalInitGLES3(void)
	{
		if(!g_initialized)
		{
		#if defined(OVR_CAPTURE_HAS_OPENGL_LOADER)
			GLES3::LoadGLFunctions();
			GLES2EXT::LoadGLFunctions();
		#endif

			const char *extensions = (const char*)glGetString(GL_EXTENSIONS);
			if(strstr(extensions, "GL_EXT_disjoint_timer_query"))
			{
				g_has_EXTdisjoint_timer_query = true;
			}

			g_initialized = true;
		}
	}


	// Clean up OpenGL state... MUST be called from the GL thread! Which is why Capture::Shutdown() can't safely call this.
	static void CleanupGLES3(void)
	{
		if(g_program2D)
		{
			glDeleteProgram(g_program2D);
			g_program2D = 0;
		}
		if(g_programArray)
		{
			glDeleteProgram(g_programArray);
			g_programArray = 0;
		}
		if(g_vertexArrayObject)
		{
			glDeleteVertexArrays(1, &g_vertexArrayObject);
			g_vertexArrayObject = 0;
		}
		for(UInt32 i=0; i<g_maxPendingFramebuffers; i++)
		{
			PendingFrameBuffer &fb = g_pendingFramebuffers[i];
			if(fb.renderbuffer) glDeleteRenderbuffers(1, &fb.renderbuffer);
			if(fb.fbo)          glDeleteFramebuffers( 1, &fb.fbo);
			if(fb.pbo)          glDeleteBuffers(      1, &fb.pbo);
			MemorySet(&fb, 0, sizeof(fb));
		}
		g_requireCleanup = false;
	}

	static void DumpShaderCompileLog(GLuint shader)
	{
		GLint msgLength = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &msgLength);
		if(msgLength > 0)
		{
			char *msg = new char[msgLength+1];
			glGetShaderInfoLog(shader, msgLength, 0, msg);
			msg[msgLength] = 0;
			while(*msg)
			{
				// retain the start of the line...
				char *line = msg;
				// Null terminate the line...
				for(; *msg; msg++)
				{
					if(*msg == '\r' || *msg == '\n')
					{
						*msg = 0;
						msg++; // goto next line...
						break;
					}
				}
				// print the line...
				if(*line) Logf(Log_Error, "GL: %s", line);
			}
			delete [] msg;
		}
	}

	static GLuint BuildShaderProgram(const char *header)
	{
		GLuint program = 0;

		GLES3ScopedState glstate;

		const char *const vertexShaderSource[] =
		{
			"#version 300 es\n",
			header,
			g_uniformShaderSource,
			( g_imageFormat==FrameBuffer_DXT1 ? g_vertexShaderSourceDXT1 : g_vertexShaderSource )
		};
		const GLuint vertexShaderSourceCount = sizeof(vertexShaderSource) / sizeof(vertexShaderSource[0]);

		const char *const fragmentShaderSource[] =
		{
			"#version 300 es\n",
			header,
			g_uniformShaderSource,
			( g_imageFormat==FrameBuffer_DXT1 ? g_fragmentShaderSourceDXT1 : g_fragmentShaderSource )
		};
		const GLuint fragmentShaderSourceCount = sizeof(fragmentShaderSource) / sizeof(fragmentShaderSource[0]);

		// Compile Vertex Shader...
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, vertexShaderSourceCount, vertexShaderSource, 0);
		glCompileShader(vertexShader);
		GLint vsuccess = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &vsuccess);
		if(vsuccess == GL_FALSE)
		{
			Logf(Log_Error, "OVR_Capture_GLES3: Failed to compile Vertex Shader!");
			DumpShaderCompileLog(vertexShader);
			glDeleteShader(vertexShader);
			vertexShader = 0;
		}

		// Compile Fragment Shader...
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, fragmentShaderSourceCount, fragmentShaderSource, 0);
		glCompileShader(fragmentShader);
		GLint fsuccess = 0;
		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fsuccess);
		if(fsuccess == GL_FALSE)
		{
			Logf(Log_Error, "OVR_Capture_GLES3: Failed to compile Fragment Shader!");
			DumpShaderCompileLog(fragmentShader);
			glDeleteShader(fragmentShader);
			fragmentShader = 0;
		}

		// Link our shaders together and build a program...
		if(vertexShader && fragmentShader)
		{
			program = glCreateProgram();
			glAttachShader(program, vertexShader);
			glAttachShader(program, fragmentShader);
			glBindAttribLocation(program, POSITION_ATTRIBUTE, "Position" );
			glBindAttribLocation(program, TEXCOORD_ATTRIBUTE, "TexCoord" );
			glLinkProgram(program);
			GLint psuccess = 0;
			glGetProgramiv(program, GL_LINK_STATUS, &psuccess);
			if(psuccess == GL_FALSE)
			{
				Logf(Log_Error, "OVR_Capture_GLES3: Failed to link Program!");
				glDeleteProgram(program);
				program = 0;
			}
			else
			{
				glstate.UseProgram(program);
				glUniform1i(static_cast<GLuint>(ShaderUniformLocations::Texture0), 0);
				if(g_imageFormat == FrameBuffer_DXT1)
				{
					glUniform2f(static_cast<GLuint>(ShaderUniformLocations::TexelSize), 1.0f/(float)g_imageWidth, 1.0f/(float)g_imageHeight);
					glUniform2f(static_cast<GLuint>(ShaderUniformLocations::UVBlockScale), (g_imageWidth-3)/(float)g_imageWidth, (g_imageHeight-3)/(float)g_imageHeight);
				}
			}
		}

		// We can safely delete our shaders now that the program holds a ref count on them...
		if(vertexShader)
		{
			glDeleteShader(vertexShader);
		}
		if(fragmentShader)
		{
			glDeleteShader(fragmentShader);
		}

		return program;
	}

	static GLuint GetProgramForTexture(const GLuint textureTarget)
	{
		if(textureTarget == GL_TEXTURE_2D)
		{
			if(!g_program2D)
			{
				const char *header = "#define TEXTURE_TYPE_2D\n";
				g_program2D = BuildShaderProgram(header);
			}
			return g_program2D;
		}
		else if(textureTarget == GL_TEXTURE_2D_ARRAY)
		{
			if(!g_programArray)
			{
				const char *header = "#define TEXTURE_TYPE_2D_ARRAY\n";
				g_programArray = BuildShaderProgram(header);
			}
			return g_programArray;
		}
		else
		{
			Logf(Log_Error, "OVR::Capture::FrameBufferGLES3(): Unknown textureTarget 0x%X!", textureTarget);
		}
		return 0;
	}

	void OnConnectGLES3(void)
	{
		g_invalidateFramebuffers = true;
		g_invalidateTimerQueries = true;
	}

	void OnDisconnectGLES3(void)
	{
		g_invalidateFramebuffers = true;
		g_invalidateTimerQueries = true;
	}


	// Captures the frame buffer from a Texture Object from an OpenGL ES 3.0 Context.
	// Must be called from a thread with a valid GLES3 context!
	void FrameBufferGLES3(const UInt32 textureTarget, const UInt32 textureID, const Rect<float> textureRect)
	{
		if(!CheckConnectionFlag(Enable_FrameBuffer_Capture))
		{
			if(g_requireCleanup)
				CleanupGLES3();
			return;
		}

		OVR_CAPTURE_CPU_ZONE(CaptureFramebuffer);

		// Make sure GL entry points are initialized
		ConditionalInitGLES3();

		// once a connection is established we should flag ourselves for cleanup...
		g_requireCleanup = true;

		if(g_invalidateFramebuffers)
		{
			// invalidate all of the framebuffers so we don't grab images from last session
			for(UInt32 i=0; i<g_maxPendingFramebuffers; i++)
			{
				PendingFrameBuffer &fb = g_pendingFramebuffers[i];
				fb.imageReady = false;
			}
			g_invalidateFramebuffers = false;
		}

		// Basic Concept:
		//   0) Capture Time Stamp
		//   1) StretchBlit into lower resolution 565 texture
		//   2) Issue async ReadPixels into pixel buffer object
		//   3) Wait N Frames
		//   4) Map PBO memory and call FrameBuffer(g_imageFormat,g_imageWidth,g_imageHeight,imageData)

		// acquire current time before spending cycles mapping and copying the PBO...
		const UInt64 currentTime = GetNanoseconds();

		// Scoped Save/Restore GL state...
		GLES3ScopedState glstate;

		// Acquire a PendingFrameBuffer container...
		PendingFrameBuffer &fb = g_pendingFramebuffers[g_nextPendingFramebuffer];

		// If the pending framebuffer has valid data in it... lets send it over the network...
		if(fb.imageReady)
		{
			OVR_CAPTURE_CPU_ZONE(MapAndCopy);
			// 4) Map PBO memory and call FrameBuffer(g_imageFormat,g_imageWidth,g_imageHeight,imageData)
			glstate.BindPixelBuffer(fb.pbo);
			const void *mappedImage = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, g_imageSize, GL_MAP_READ_BIT);
			FrameBuffer(fb.timestamp, g_imageFormat, g_imageWidth, g_imageHeight, mappedImage);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			fb.imageReady = false;
		}

		// 0) Capture Time Stamp
		fb.timestamp = currentTime;

		// Create GL objects if necessary...
		if(!fb.renderbuffer)
		{
			glGenRenderbuffers(1, &fb.renderbuffer);
			glstate.BindRenderbuffer(fb.renderbuffer);
			glRenderbufferStorage(GL_RENDERBUFFER, g_imageFormatGL, g_imageWidthBlocks, g_imageHeightBlocks);
		}
		if(!fb.fbo)
		{
			glGenFramebuffers(1, &fb.fbo);
			glstate.BindFramebuffer(fb.fbo);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.renderbuffer);
			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if(status != GL_FRAMEBUFFER_COMPLETE)
			{
				Logf(Log_Error, "OVR::Capture::FrameBufferGLES3(): Failed to create valid FBO!");
				fb.fbo = 0;
			}
		}
		if(!fb.pbo)
		{
			glGenBuffers(1, &fb.pbo);
			glstate.BindPixelBuffer(fb.pbo);
			glBufferData(GL_PIXEL_PACK_BUFFER, g_imageSize, nullptr, GL_DYNAMIC_READ);
		}

		// Create Vertex Array...
		if(!g_vertexArrayObject)
		{
			glGenVertexArrays(1, &g_vertexArrayObject);
			glstate.BindVertexArray(g_vertexArrayObject);

			GLuint vbo = 0;
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertices), g_vertices, GL_STATIC_DRAW);

			glEnableVertexAttribArray(POSITION_ATTRIBUTE);
			glEnableVertexAttribArray(TEXCOORD_ATTRIBUTE);
			glVertexAttribPointer(POSITION_ATTRIBUTE, 4, GL_FLOAT, GL_FALSE, sizeof(float)*6, ((const char*)nullptr)+sizeof(float)*0);
			glVertexAttribPointer(TEXCOORD_ATTRIBUTE, 2, GL_FLOAT, GL_FALSE, sizeof(float)*6, ((const char*)nullptr)+sizeof(float)*4);

			// Safe to delete client reference now that VAO holds a reference count.
			// We unbind the VAO temporarily though because deleting a bound buffer automatically unbinds it which in
			// this case results in the VAO losing its reference to the VBO.
			glstate.BindVertexArray(0);
			glDeleteBuffers( 1, &vbo );
		}

		// Get the shader program we need for this texture... compiles on-demand.
		const GLuint program = GetProgramForTexture(textureTarget);

		// Verify all the objects we need are allocated...
		if(!fb.renderbuffer || !fb.fbo || !fb.pbo || !program || !g_vertexArrayObject)
			return;

		// 1) StretchBlit into lower resolution 565 texture

		// Override OpenGL State...
		if(true)
		{
			OVR_CAPTURE_CPU_ZONE(BindState);

			glstate.BindFramebuffer(fb.fbo);

			glstate.Viewport(0, 0, g_imageWidthBlocks, g_imageHeightBlocks);

			glstate.Disable(glstate.DEPTH_TEST);
			glstate.Disable(glstate.SCISSOR_TEST);
			glstate.Disable(glstate.STENCIL_TEST);
			glstate.Disable(glstate.RASTERIZER_DISCARD);
			glstate.Disable(glstate.DITHER);
			glstate.Disable(glstate.CULL_FACE); // turning culling off entirely is one less state than setting winding order and front face.
			glstate.Disable(glstate.BLEND);

			glstate.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

			glstate.BindTexture(glstate.TEXTURE0, textureTarget, textureID);
			glstate.UseProgram(program);

			// TextureRect...
			glUniform4f(static_cast<GLuint>(ShaderUniformLocations::TextureRect), textureRect.x, textureRect.y, textureRect.width, textureRect.height);

			glstate.BindVertexArray(g_vertexArrayObject);
		}

		const GLenum attachments[1] = { GL_COLOR_ATTACHMENT0 };
		glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, attachments);

		// Useful for detecting GL state leaks that kill drawing...
		// Because only a few state bits affect glClear (pixel ownership, dithering, color mask, and scissor), enabling this
		// helps narrow down if a the FB is not being updated because of further state leaks that affect rendering geometry,
		// or if its another issue.
		//glClearColor(0,0,1,1);
		//glClear(GL_COLOR_BUFFER_BIT);

		// Blit draw call...
		if(true)
		{
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}

		// 2) Issue async ReadPixels into pixel buffer object
		if(true)
		{
			OVR_CAPTURE_CPU_ZONE(ReadPixels);
			glstate.BindFramebuffer(fb.fbo);
			glstate.BindPixelBuffer(fb.pbo);
			if(g_imageFormatGL == GL_RGB565)
				glReadPixels(0, 0, g_imageWidthBlocks, g_imageHeightBlocks, GL_RGB,          GL_UNSIGNED_SHORT_5_6_5, 0);
			else if(g_imageFormatGL == GL_RGBA8)
				glReadPixels(0, 0, g_imageWidthBlocks, g_imageHeightBlocks, GL_RGBA,         GL_UNSIGNED_BYTE,        0);
			else if(g_imageFormatGL == GL_RGBA16UI)
				glReadPixels(0, 0, g_imageWidthBlocks, g_imageHeightBlocks, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT,       0);
			else if(g_imageFormatGL == GL_RGBA16I)
				glReadPixels(0, 0, g_imageWidthBlocks, g_imageHeightBlocks, GL_RGBA_INTEGER, GL_SHORT,                0);
			fb.imageReady = true;
		}

		// Increment to the next PendingFrameBuffer...
		g_nextPendingFramebuffer = (g_nextPendingFramebuffer+1)&(g_maxPendingFramebuffers-1);
	}


	// We will have a circular buffer of GPU timer queries which will will take ownership of inside Enter/Leave...
	// And every frame we will iterate through all the completed queries and dispatch the ones that are complete...
	// The circular buffer size therefor should be at least big enough to hold all the outstanding queries for about 2 frames.
	// On Qualcomm chips we will need to check for disjoint events and send a different packet when that happens... as this indicates
	// that the timings are unreliable. This is probably also a worthwhile event to mark in the time-line as it probably will
	// coincide with a hitch or poor performance event.
	// EXTdisjoint_timer_query
	// https://www.khronos.org/registry/gles/extensions/EXT/EXTdisjoint_timer_query.txt

	struct TimerQuery
	{
		GLuint queryID;
		UInt32 labelID; // 0 for Leave event...
	};

	typedef std::deque<TimerQuery> TimerQueryBuffer;

	struct TimerQueryPool
	{
		TimerQueryBuffer available;
		TimerQueryBuffer pending;
	};

	static TimerQueryPool *CreateTimerQueryPool(void)
	{
		TimerQueryPool *pool = new TimerQueryPool;
		for(UInt32 i=0; i<g_maxTimerQueries; i++)
		{
			TimerQuery q = {};
			glGenQueriesEXT(1, &q.queryID);
			OVR_CAPTURE_ASSERT(q.queryID);
			pool->available.push_back(q);
		}
		return pool;
	}

	static void DestroyTimerQueryPool(void *value)
	{
		TimerQueryPool *pool = (TimerQueryPool*)value;
		// Note: we intentionally don't clean up the queryIDs because its almost certain that the
		//       OpenGL context has been destroyed before the thread is destroyed.
		delete pool;
	}

	static TimerQueryPool &AcquireTimerQueryPool(void)
	{
		static ThreadLocalKey key = CreateThreadLocalKey(DestroyTimerQueryPool);
		TimerQueryPool *pool = (TimerQueryPool*)GetThreadLocalValue(key);
		if(!pool)
		{
			pool = CreateTimerQueryPool();
			SetThreadLocalValue(key, pool);
		}
		return *pool;
	}

	static void SendGPUSyncPacket(void)
	{
		// This is used to compare our own monotonic clock to the GL one...
		// OVRMonitor use the difference between these two values to sync up GPU and CPU events.
		GPUClockSyncPacket clockSyncPacket;
		clockSyncPacket.timestampCPU = GetNanoseconds();
		glGetInteger64v(GL_TIMESTAMP_EXT, (GLint64*)&clockSyncPacket.timestampGPU);
		AsyncStream::Acquire()->WritePacket(clockSyncPacket);
	}

	static void InvalidateTimerQueries(void)
	{
		// Clear the pending buffer...
		TimerQueryPool &pool = AcquireTimerQueryPool();
		while(!pool.pending.empty())
		{
			const TimerQuery query = pool.pending.front();
			pool.pending.pop_front();
			pool.available.push_back(query);
		}
		// resync with the GPU clock...
		SendGPUSyncPacket();
	}

	static void ProcessTimerQueries(void)
	{
		TimerQueryPool   &pool      = AcquireTimerQueryPool();
		TimerQueryBuffer &available = pool.available;
		TimerQueryBuffer &pending   = pool.pending;

		// Nothing pending, just abort quickly!
		if(pending.empty())
			return;

		while(true)
		{
			while(true)
			{
				// If no more queries are pending, we are done...
				if(pending.empty())
					break;

				const TimerQuery query = pending.front();

				// Check to see if results are ready...
				GLuint ready = GL_FALSE;
				glGetQueryObjectuivEXT(query.queryID, GL_QUERY_RESULT_AVAILABLE_EXT, &ready);

				// If results not ready stop trying to flush results as any remaining queries will also fail... we will try again later...
				if(!ready)
					break;

				// Read timer value back...
				GLuint64 gputimestamp = 0;
				glGetQueryObjectui64vEXT(query.queryID, GL_QUERY_RESULT, &gputimestamp);

				if(query.labelID)
				{
					// Send our enter packet...
					GPUZoneEnterPacket packet;
					packet.labelID   = query.labelID;
					packet.timestamp = gputimestamp;
					AsyncStream::Acquire()->WritePacket(packet);
				}
				else
				{
					// Send leave packet...
					GPUZoneLeavePacket packet;
					packet.timestamp = gputimestamp;
					AsyncStream::Acquire()->WritePacket(packet);
				}

				// Move from pending list to available list...
				pending.pop_front();
				available.push_back(query);
			} // while(true)

			// if the ring is full, we keep trying until we have room for more events.
			if(!available.empty())
				break;

			// force GL to flush pending commands to prevent a deadlock...
			Log(Log_Warning, "Timer Query: buffer full! Forcing a glFlush!");
			glFlush();
		} // while(true)

		// Check for disjoint errors...
		GLint disjointOccured = GL_FALSE;
		glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjointOccured);
		if(disjointOccured)
		{
			// TODO: send some sort of disjoint error packet so that OVRMonitor can mark it on the time-line.
			//       because this error likely coincides with bad GPU data and/or a major performance event.
			Log(Log_Warning, "Timer Query: Disjoint event!");
			SendGPUSyncPacket();
		}
	}

	void EnterGPUZoneGLES3(const Label &label)
	{
		if(!TryLockConnection(Enable_GPU_Zones))
			return;

		ConditionalInitGLES3();

		if(g_has_EXTdisjoint_timer_query)
		{
			if(g_invalidateTimerQueries)
			{
				InvalidateTimerQueries();
				g_invalidateTimerQueries = false;
			}

			ProcessTimerQueries();

			TimerQueryPool &pool  = AcquireTimerQueryPool();
			TimerQuery      query = pool.available.front();
			glQueryCounterEXT(query.queryID, GL_TIMESTAMP_EXT);
			query.labelID = ComputeLabelHash(label);
			pool.available.pop_front();
			pool.pending.push_back(query);
		}

		UnlockConnection();
	}

	void LeaveGPUZoneGLES3(void)
	{
		if(!TryLockConnection(Enable_GPU_Zones))
			return;

		OVR_CAPTURE_ASSERT(g_initialized);
		
		if(g_has_EXTdisjoint_timer_query)
		{
			ProcessTimerQueries();

			TimerQueryPool &pool  = AcquireTimerQueryPool();
			TimerQuery      query = pool.available.front();
			glQueryCounterEXT(query.queryID, GL_TIMESTAMP_EXT);
			query.labelID = 0;
			pool.available.pop_front();
			pool.pending.push_back(query);
		}

		UnlockConnection();
	}

} // namespace Capture
} // namespace OVR

#endif // #if defined(OVR_CAPTURE_HAS_GLES3)
