/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions and/or use of the source code (partially or complete) must retain
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form (partially or complete) must reproduce
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
         * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
       * Neither the name of the organization nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
       * Military use is not permitted.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE AUTHOR (PETRU SOROAGA) BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "../base/base.h"
#include "../base/config.h"
#include "../r_central/shared_vars.h"
#include "../r_central/keyboard.h"

// Desktop input state (set by SDL event processing)
extern bool g_bQuit; // Defined in shared_vars.cpp
int  g_lastKeyPressed = 0;
int  g_mouseX = 0, g_mouseY = 0;
bool g_mouseButtonLeft = false, g_mouseButtonRight = false, g_mouseButtonMiddle = false;

#include "render_engine_desktop.h"
#include "lodepng.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <utime.h>
#include <getopt.h>
#include <endian.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>

static int s_iLastCairoFontFamilyId = -1;
static bool s_bLastCairoFontStyleBold = false;

// Default screen size for desktop (will be overridden by config)
#define DEFAULT_SCREEN_WIDTH  800
#define DEFAULT_SCREEN_HEIGHT 480

RenderEngineDesktop::RenderEngineDesktop()
: RenderEngine()
{
   log_line("[RenderEngineDesktop] RendererDesktop: Init started.");
   
   m_bMustTestFontAccess = true;
   m_bHasNewFont = false;
   m_bUseDoubleBuffering = true;
   m_pCairoCtx = NULL;
   m_pCairoTempCtx = NULL;
   m_fStrokeSizePx = 1.0;

   // Initialize SDL2 subsystems
   m_pSDLWindow = NULL;
   m_pSDLSurface = NULL;
   m_pSDLTexture = NULL;
   m_bSDLInitialized = false;
   if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      m_bSDLInitialized = false;
      log_softerror_and_alarm("[RenderEngineDesktop] SDL_Init failed: %s", SDL_GetError());
   } else {
      m_bSDLInitialized = true;
      log_line("[RenderEngineDesktop] SDL2 initialized successfully.");
   }

   // Initialize with default size; initWindow() will be called from initEngine()
   m_iRenderWidth  = DEFAULT_SCREEN_WIDTH;
   m_iRenderHeight = DEFAULT_SCREEN_HEIGHT;
   m_fPixelWidth   = 1.0f / (float)m_iRenderWidth;
   m_fPixelHeight  = 1.0f / (float)m_iRenderHeight;
   
   // Initialize other member variables
   m_iActiveSurface = 0;
   m_pVideoFrameARGB = NULL;
   m_iVideoFrameWidth = 0;
   m_iVideoFrameHeight = 0;
   m_uRenderDrawSurfacesIds[0] = 0;
   m_uRenderDrawSurfacesIds[1] = 1;

   // Initialize image/icon arrays
   m_iCountImages = 0;
   m_iCountIcons  = 0;
   m_CurrentImageId = 0;
   m_CurrentIconId  = 0;
   
   for (int i = 0; i < MAX_RAW_IMAGES; i++) {
      m_pImages[i] = NULL;
      m_ImageIds[i] = 0;
   }
   for (int i = 0; i < MAX_RAW_ICONS; i++) {
      m_pIcons[i] = NULL;
      m_IconIds[i] = 0;
      for (int j = 0; j < 2; j++) m_pIconsMip[i][j] = NULL;
   }

   log_line("[RenderEngineDesktop] Render init done.");
}

RenderEngineDesktop::~RenderEngineDesktop()
{
   log_line("[RenderEngineDesktop] Destructor: cleaning up...");
   
   // Free all loaded images and icons
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_pImages[i]) cairo_surface_destroy(m_pImages[i]);
   }
   for (int i = 0; i < m_iCountIcons; i++) {
      if (m_pIcons[i]) cairo_surface_destroy(m_pIcons[i]);
      for (int j = 0; j < 2; j++) {
         if (m_pIconsMip[i][j]) cairo_surface_destroy(m_pIconsMip[i][j]);
      }
   }

   // Destroy Cairo contexts and surfaces
   if (m_pCairoCtx)     cairo_destroy(m_pCairoCtx);
   if (m_pCairoTempCtx) cairo_destroy(m_pCairoTempCtx);
   for (int i = 0; i < 2; i++) {
      if (m_pMainCairoSurface[i]) cairo_surface_destroy(m_pMainCairoSurface[i]);
   }

   // Free back buffers
   for (int i = 0; i < 2; i++) {
      if (m_pBackBuffer[i]) free(m_pBackBuffer[i]);
   }

   // Destroy SDL resources
   if (m_pSDLTexture)  SDL_DestroyTexture(m_pSDLTexture);
   if (m_pSDLSurface) SDL_DestroyRenderer(m_pSDLSurface);
   if (m_pSDLWindow)  SDL_DestroyWindow(m_pSDLWindow);

   // SDL_Quit() is called only if the video subsystem was initialized: it
   // would report a spurious error otherwise.
   if (m_bSDLInitialized)
      SDL_Quit();
   log_line("[RenderEngineDesktop] Cleanup done.");
}

bool RenderEngineDesktop::initEngine()
{
   log_line("[RenderEngineDesktop] initEngine: initializing window...");

   // Without the SDL video subsystem there is no window and no draw context, so
   // the engine cannot be used at all. This happens when the program is started
   // with sudo and the session environment (XDG_RUNTIME_DIR, WAYLAND_DISPLAY)
   // is not passed through: SDL cannot reach the Wayland compositor.
   if ( ! m_bSDLInitialized )
   {
      log_softerror_and_alarm("[RenderEngineDesktop] The SDL video subsystem is not initialized, the UI cannot be started. "
         "If the program was started with sudo, the session environment is missing: run it with "
         "'sudo XDG_RUNTIME_DIR=%s WAYLAND_DISPLAY=%s ./ruby_central'.",
         getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/run/user/<uid>",
         getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "wayland-0");
      return false;
   }

   // Initialize the SDL2 window with default size
   if (initWindow(m_iRenderWidth, m_iRenderHeight) != 0) {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to initialize window");
      return false;
   }

   log_line("[RenderEngineDesktop] initEngine: window initialized successfully (%dx%d)", m_iRenderWidth, m_iRenderHeight);
   return true;
}

int RenderEngineDesktop::initWindow(int width, int height)
{
   m_iRenderWidth  = width;
   m_iRenderHeight = height;
   m_fPixelWidth   = 1.0f / (float)m_iRenderWidth;
   m_fPixelHeight  = 1.0f / (float)m_iRenderHeight;

   log_line("[RenderEngineDesktop] Creating window: %dx%d", m_iRenderWidth, m_iRenderHeight);

   // Create SDL window
   m_pSDLWindow = SDL_CreateWindow("RubyFPV GCS",
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    m_iRenderWidth, m_iRenderHeight,
                                    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   if (!m_pSDLWindow) {
      log_softerror_and_alarm("[RenderEngineDesktop] SDL_CreateWindow failed: %s", SDL_GetError());
      return -1;
   }

   // Create a software renderer first (more reliable in headless/X11 environments)
   m_pSDLSurface = SDL_CreateRenderer(m_pSDLWindow, -1, SDL_RENDERER_SOFTWARE);
   if (!m_pSDLSurface) {
      log_softerror_and_alarm("[RenderEngineDesktop] SDL_CreateRenderer failed: %s", SDL_GetError());
      // Fall back to software renderer
      m_pSDLSurface = SDL_CreateRenderer(m_pSDLWindow, -1, SDL_RENDERER_SOFTWARE);
      if (!m_pSDLSurface) {
         log_softerror_and_alarm("[RenderEngineDesktop] SDL_CreateRenderer (software fallback) failed: %s", SDL_GetError());
         return -1;
      }
   }

   // Allocate back buffers (double-buffered ARGB32)
   int stride = m_iRenderWidth * 4; // ARGB32 = 4 bytes per pixel
   for (int i = 0; i < 2; i++) {
      m_pBackBuffer[i] = (Uint32*)calloc(m_iRenderWidth * m_iRenderHeight, sizeof(Uint32));
      if (!m_pBackBuffer[i]) {
         log_softerror_and_alarm("[RenderEngineDesktop] Failed to allocate back buffer %d", i);
         return -1;
      }
   }

   // Create Cairo image surfaces backed by the back buffers
   m_uRenderDrawSurfacesIds[0] = 0;
   m_uRenderDrawSurfacesIds[1] = 1;

   m_pMainCairoSurface[0] = cairo_image_surface_create_for_data(
      (unsigned char*)m_pBackBuffer[0], CAIRO_FORMAT_ARGB32,
      m_iRenderWidth, m_iRenderHeight, stride);
   
   m_pMainCairoSurface[1] = cairo_image_surface_create_for_data(
      (unsigned char*)m_pBackBuffer[1], CAIRO_FORMAT_ARGB32,
      m_iRenderWidth, m_iRenderHeight, stride);

   if (!m_pMainCairoSurface[0] || !m_pMainCairoSurface[1]) {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to create Cairo surfaces");
      return -1;
   }

   // Create the main Cairo context (will be switched between surfaces)
   m_pCairoCtx = cairo_create(m_pMainCairoSurface[m_iActiveSurface]);
   if (!m_pCairoCtx) {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to create Cairo context");
      return -1;
   }

   // Clear both buffers to black
   for (int i = 0; i < 2; i++) {
      cairo_t* ctx = cairo_create(m_pMainCairoSurface[i]);
      if (ctx) {
         cairo_set_source_rgb(ctx, 0, 0, 0);
         cairo_paint(ctx);
         cairo_destroy(ctx);
      }
   }

   log_line("[RenderEngineDesktop] Display size: %dx%d, pixel size: %.4fx%.4f",
            m_iRenderWidth, m_iRenderHeight, m_fPixelWidth, m_fPixelHeight);
   log_line("[RenderEngineDesktop] Created back buffers and Cairo surfaces.");

   return 0;
}

// Maps an SDL key symbol to a RubyFPV input event.
// Returns 0 if the key is not mapped.
static u32 _sdl_key_to_input_event(SDL_Keycode sym)
{
   switch (sym) {
      case SDLK_MENU:
      case SDLK_TAB:
         return INPUT_EVENT_PRESS_MENU;

      case SDLK_BACKSPACE:
      case SDLK_LEFT:
      case SDLK_UP:
         return INPUT_EVENT_PRESS_BACK;

      case SDLK_PLUS:
      case SDLK_EQUALS:
      case SDLK_KP_PLUS:
      case SDLK_RIGHT:
      case SDLK_DOWN:
         return INPUT_EVENT_PRESS_PLUS;

      case SDLK_MINUS:
      case SDLK_KP_MINUS:
         return INPUT_EVENT_PRESS_MINUS;

      case SDLK_1:
         return INPUT_EVENT_PRESS_QA1;
      case SDLK_2:
         return INPUT_EVENT_PRESS_QA2;
      case SDLK_3:
         return INPUT_EVENT_PRESS_QA3;

      case SDLK_F1:
         return INPUT_EVENT_PRESS_QAPLUS;
      case SDLK_F2:
         return INPUT_EVENT_PRESS_QAMINUS;

      default:
         return 0;
   }
}

bool RenderEngineDesktop::processEvent(SDL_Event* event)
{
   if (!event) return true;

   switch (event->type) {
      case SDL_QUIT:
         g_bQuit = true;
         return false;

      case SDL_KEYDOWN:
      {
         SDL_Keycode sym = event->key.keysym.sym;

         if ( SDLK_ESCAPE == sym )
         {
            g_bQuit = true;
            break;
         }

         // Store the last key pressed for UI to read
         g_lastKeyPressed = sym;

         u32 uEvent = _sdl_key_to_input_event(sym);
         if ( 0 == uEvent )
            break;

         // The key press is registered only once per physical press: the
         // auto-repeat generated by the window system while the key is held
         // down is filtered out here. The auto-repeat is handled separately,
         // as a long press.
         if ( event->key.repeat )
            break;

         keyboard_add_key_press((int)sym, uEvent);
         break;
      }

      case SDL_KEYUP:
      {
         SDL_Keycode sym = event->key.keysym.sym;
         if ( _sdl_key_to_input_event(sym) != 0 )
            keyboard_add_key_release((int)sym);
         break;
      }

      case SDL_MOUSEMOTION:
         // Track mouse position for UI interaction
         g_mouseX = event->motion.x;
         g_mouseY = event->motion.y;
         break;

      case SDL_MOUSEBUTTONDOWN:
         g_mouseX = event->button.x;
         g_mouseY = event->button.y;
         switch (event->button.button) {
            case SDL_BUTTON_LEFT:   g_mouseButtonLeft = true;  break;
            case SDL_BUTTON_RIGHT:  g_mouseButtonRight = true; break;
            case SDL_BUTTON_MIDDLE: g_mouseButtonMiddle = true; break;
         }
         break;

      case SDL_MOUSEBUTTONUP:
         g_mouseX = event->button.x;
         g_mouseY = event->button.y;
         switch (event->button.button) {
            case SDL_BUTTON_LEFT:   g_mouseButtonLeft = false;  break;
            case SDL_BUTTON_RIGHT:  g_mouseButtonRight = false; break;
            case SDL_BUTTON_MIDDLE: g_mouseButtonMiddle = false; break;
         }
         break;

      case SDL_WINDOWEVENT:
         if (event->window.event == SDL_WINDOWEVENT_RESIZED) {
            // Handle window resize: reallocate the back buffers, recreate the
            // Cairo surfaces and the SDL texture at the new size. The UI and
            // the video underlay are redrawn at the new dimensions on the
            // next frame, so the video is never stretched artificially: it is
            // re-scaled, aspect fit, into the new window size.
            int w = event->window.data1;
            int h = event->window.data2;
            if ( w > 0 && h > 0 && (w != m_iRenderWidth || h != m_iRenderHeight) && m_bSDLInitialized )
            {
               log_line("[RenderEngineDesktop] Window resized to %dx%d", w, h);

               int stride = w * 4;
               for ( int i = 0; i < 2; i++ )
               {
                  if ( m_pBackBuffer[i] )
                  {
                     free(m_pBackBuffer[i]);
                     m_pBackBuffer[i] = NULL;
                  }
                  m_pBackBuffer[i] = (Uint32*)calloc(w * h, sizeof(Uint32));
                  if ( NULL == m_pBackBuffer[i] )
                  {
                     log_softerror_and_alarm("[RenderEngineDesktop] Failed to allocate back buffer %d (%dx%d) after resize.", i, w, h);
                     return true;
                  }
               }

               if ( m_pCairoCtx )
               {
                  cairo_destroy(m_pCairoCtx);
                  m_pCairoCtx = NULL;
               }
               if ( m_pCairoTempCtx )
               {
                  cairo_destroy(m_pCairoTempCtx);
                  m_pCairoTempCtx = NULL;
               }

               m_iActiveSurface = 0;

               for ( int i = 0; i < 2; i++ )
               {
                  if ( NULL != m_pMainCairoSurface[i] )
                  {
                     cairo_surface_destroy(m_pMainCairoSurface[i]);
                  }
                  m_pMainCairoSurface[i] = cairo_image_surface_create_for_data(
                     (unsigned char*)m_pBackBuffer[i], CAIRO_FORMAT_ARGB32, w, h, stride);
                  if ( NULL == m_pMainCairoSurface[i] )
                  {
                     log_softerror_and_alarm("[RenderEngineDesktop] Failed to recreate Cairo surface %d after resize.", i);
                  }
               }

               if ( NULL != m_pSDLTexture )
               {
                  SDL_DestroyTexture(m_pSDLTexture);
                  m_pSDLTexture = NULL;
               }

               m_iRenderWidth = w;
               m_iRenderHeight = h;
               m_fPixelWidth = 1.0f / (float)w;
               m_fPixelHeight = 1.0f / (float)h;

               // Recreate the active draw context on the fresh surface.
               m_pCairoCtx = cairo_create(m_pMainCairoSurface[m_iActiveSurface]);
               // Signal that the rendering surface changed, so that the UI
               // frameworks using engine surfaces can reinitialize themselves.
               m_uRenderDrawSurfacesIds[0]++;
               m_uRenderDrawSurfacesIds[1]++;
               log_line("[RenderEngineDesktop] Window resized: buffers and surfaces recreated at %dx%d.", w, h);
            }
         }
         break;

      default:
         break;
   }

   return true;
}

void RenderEngineDesktop::presentFrame()
{
   if (!m_pSDLSurface || !m_pSDLWindow) return;
   if (NULL == m_pMainCairoSurface[m_iActiveSurface]) return;
   if (NULL == m_pBackBuffer[m_iActiveSurface]) return;

   // First, flush any pending Cairo operations on the active surface
   cairo_surface_flush(m_pMainCairoSurface[m_iActiveSurface]);

   // The texture has to be a streaming one: SDL_LockTexture() only works on
   // streaming textures, it fails on a target texture (and would leave the
   // pixel pointer invalid).
   if (NULL == m_pSDLTexture)
   {
      m_pSDLTexture = SDL_CreateTexture(m_pSDLSurface, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, m_iRenderWidth, m_iRenderHeight);
      if (NULL == m_pSDLTexture)
      {
         log_softerror_and_alarm("[RenderEngineDesktop] Failed to create SDL texture: %s", SDL_GetError());
         return;
      }
      SDL_SetTextureBlendMode(m_pSDLTexture, SDL_BLENDMODE_NONE);
   }

   // Copy the Cairo back buffer into the texture. Cairo may use a stride
   // different from width*4, so the rows are copied one by one.
   void* pixels = NULL;
   int pitch = 0;
   if (0 != SDL_LockTexture(m_pSDLTexture, NULL, &pixels, &pitch))
   {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to lock SDL texture: %s", SDL_GetError());
      return;
   }

   int iCairoStride = cairo_image_surface_get_stride(m_pMainCairoSurface[m_iActiveSurface]);
   int iCopyBytes = m_iRenderWidth * 4;
   if (pitch < iCopyBytes)
      iCopyBytes = pitch;

   const unsigned char* pSrc = (const unsigned char*)m_pBackBuffer[m_iActiveSurface];
   unsigned char* pDst = (unsigned char*)pixels;
   for (int y = 0; y < m_iRenderHeight; y++)
   {
      memcpy(pDst + (size_t)y * pitch, pSrc + (size_t)y * iCairoStride, iCopyBytes);
   }

   SDL_UnlockTexture(m_pSDLTexture);

   // Clear and present
   SDL_SetRenderTarget(m_pSDLSurface, NULL);
   SDL_RenderClear(m_pSDLSurface);
   SDL_RenderCopy(m_pSDLSurface, m_pSDLTexture, NULL, NULL);
   SDL_RenderPresent(m_pSDLSurface);
}

void* RenderEngineDesktop::getDrawContext()
{
   return _getActiveCairoContext();
}

void RenderEngineDesktop::setStroke(const double* color, float fStrokeSize)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float fAlpha = color[3] * m_fGlobalAlfa;
   if (fAlpha > 1.0) fAlpha = 1.0;
   if (fAlpha < 0.0) fAlpha = 0.0;

   m_ColorStroke[0] = color[0];
   m_ColorStroke[1] = color[1];
   m_ColorStroke[2] = color[2];
   m_ColorStroke[3] = fAlpha * 255;
   m_fStrokeSizePx = fStrokeSize;

   if (fStrokeSize < 0.5) {
      m_fStrokeSizePx = fStrokeSize / m_fPixelWidth;
   }

   // The color components are 0..255, but Cairo expects 0.0..1.0.
   cairo_set_source_rgba(ctx, color[0]/255.0, color[1]/255.0, color[2]/255.0, fAlpha);
   // m_fStrokeSizePx is already expressed in pixels.
   cairo_set_line_width(ctx, m_fStrokeSizePx);
}

void RenderEngineDesktop::setStrokeSize(float fStrokeSize)
{
   m_fStrokeSizePx = fStrokeSize;
   if (fStrokeSize < 0.5) {
      m_fStrokeSizePx = fStrokeSize / m_fPixelWidth;
   }
   
   cairo_t* ctx = _getActiveCairoContext();
   if (ctx) {
      // m_fStrokeSizePx is already expressed in pixels.
      cairo_set_line_width(ctx, m_fStrokeSizePx);
   }
}

void RenderEngineDesktop::setFontOutlineColor(u32 idFont, u8 r, u8 g, u8 b, u8 a)
{
   // Not implemented for desktop - text outline handled via Cairo stroke
   (void)idFont; (void)r; (void)g; (void)b; (void)a;
}

u32 RenderEngineDesktop::loadImage(const char* szFile)
{
   if (!szFile || m_iCountImages >= MAX_RAW_IMAGES) return 0;

   if (access(szFile, R_OK) == -1) {
      log_softerror_and_alarm("[RenderEngineDesktop] Can't access image file: %s", szFile);
      return 0;
   }

   // Use cairo_image_surface_create_from_png: it owns the pixel data, so the
   // surface stays valid for as long as it is used (e.g. by changeImageHue()).
   // Creating a surface over a lodepng buffer and then freeing that buffer
   // leaves the surface pointing at freed memory.
   cairo_surface_t* surf = cairo_image_surface_create_from_png(szFile);

   if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to load image: %s (%s)", szFile, cairo_status_to_string(cairo_surface_status(surf)));
      cairo_surface_destroy(surf);
      return 0;
   }

   int w = cairo_image_surface_get_width(surf);
   int h = cairo_image_surface_get_height(surf);

   u32 id = ++m_CurrentImageId;
   m_pImages[m_iCountImages] = surf;
   m_ImageIds[m_iCountImages] = id;
   m_iCountImages++;

   log_line("[RenderEngineDesktop] Loaded image '%s': %dx%d, id=%u (count: %d)", szFile, w, h, id, m_iCountImages);
   return id;
}

void RenderEngineDesktop::freeImage(u32 idImage)
{
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_ImageIds[i] == idImage) {
         cairo_surface_destroy(m_pImages[i]);
         m_pImages[i] = NULL;
         // Shift remaining images down
         for (int j = i; j < m_iCountImages - 1; j++) {
            m_pImages[j] = m_pImages[j + 1];
            m_ImageIds[j] = m_ImageIds[j + 1];
         }
         m_pImages[m_iCountImages - 1] = NULL;
         m_iCountImages--;
         log_line("[RenderEngineDesktop] Freed image id %u, remaining: %d", idImage, m_iCountImages);
         return;
      }
   }
}

u32 RenderEngineDesktop::loadIcon(const char* szFile)
{
   if (!szFile || m_iCountIcons >= MAX_RAW_ICONS) return 0;

   if (access(szFile, R_OK) == -1) {
      log_softerror_and_alarm("[RenderEngineDesktop] Can't access icon file: %s", szFile);
      return 0;
   }

   // Same as loadImage(): the surface must own its pixel data.
   cairo_surface_t* surf = cairo_image_surface_create_from_png(szFile);

   if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to load icon: %s (%s)", szFile, cairo_status_to_string(cairo_surface_status(surf)));
      cairo_surface_destroy(surf);
      return 0;
   }

   int w = cairo_image_surface_get_width(surf);
   int h = cairo_image_surface_get_height(surf);

   u32 id = ++m_CurrentIconId;
   m_pIcons[m_iCountIcons] = surf;
   m_pIconsMip[m_iCountIcons][0] = NULL;
   m_pIconsMip[m_iCountIcons][1] = NULL;
   m_IconIds[m_iCountIcons] = id;
   m_iCountIcons++;

   log_line("[RenderEngineDesktop] Loaded icon '%s': %dx%d, id=%u", szFile, w, h, id);
   return id;
}

void RenderEngineDesktop::freeIcon(u32 idIcon)
{
   for (int i = 0; i < m_iCountIcons; i++) {
      if (m_IconIds[i] == idIcon) {
         cairo_surface_destroy(m_pIcons[i]);
         m_pIcons[i] = NULL;
         for (int j = i; j < m_iCountIcons - 1; j++) {
            m_pIcons[j] = m_pIcons[j + 1];
            m_IconIds[j] = m_IconIds[j + 1];
         }
         m_pIcons[m_iCountIcons - 1] = NULL;
         for (int j = 0; j < 2; j++) m_pIconsMip[m_iCountIcons][j] = NULL;
         m_iCountIcons--;
         return;
      }
   }
}

int RenderEngineDesktop::getImageWidth(u32 uImageId)
{
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_ImageIds[i] == uImageId && m_pImages[i]) {
         return (int)cairo_image_surface_get_width(m_pImages[i]);
      }
   }
   return 0;
}

int RenderEngineDesktop::getImageHeight(u32 uImageId)
{
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_ImageIds[i] == uImageId && m_pImages[i]) {
         return (int)cairo_image_surface_get_height(m_pImages[i]);
      }
   }
   return 0;
}

void RenderEngineDesktop::changeImageHue(u32 uImageId, u8 r, u8 g, u8 b)
{
   if ( uImageId < 1 )
      return;

   int indexImage = -1;
   for( int i=0; i<m_iCountImages; i++ )
   {
      if ( m_ImageIds[i] == uImageId )
      {
         indexImage = i;
         break;
      }
   }
   if ( (-1 == indexImage) || (NULL == m_pImages[indexImage]) )
      return;

   cairo_surface_flush(m_pImages[indexImage]);

   int iWidth = cairo_image_surface_get_width(m_pImages[indexImage]);
   int iHeight = cairo_image_surface_get_height(m_pImages[indexImage]);
   int iImageStride = cairo_image_surface_get_stride(m_pImages[indexImage]);
   u8* pImageData = cairo_image_surface_get_data(m_pImages[indexImage]);
   if ( NULL == pImageData )
      return;

   // Cairo ARGB32 is stored as B,G,R,A bytes in little endian, with the
   // color channels premultiplied by alpha.
   for( int y=0; y<iHeight; y++ )
   {
      u8* pDestLine = (u8*)(pImageData + y * iImageStride);
      for( int x=0; x<iWidth; x++ )
      {
         u8* pPixel = pDestLine + x*4;
         u8 uBlue = pPixel[0];
         u8 uGreen = pPixel[1];
         u8 uRed = pPixel[2];
         u8 uAlpha = pPixel[3];

         if ( uAlpha > 0 )
         {
            // Undo the premultiplication to recover the real color components.
            u32 uRedReal = ((u32)uRed * 255) / uAlpha;
            u32 uGreenReal = ((u32)uGreen * 255) / uAlpha;
            u32 uBlueReal = ((u32)uBlue * 255) / uAlpha;
            if ( uRedReal > 255 ) uRedReal = 255;
            if ( uGreenReal > 255 ) uGreenReal = 255;
            if ( uBlueReal > 255 ) uBlueReal = 255;

            // Only near white pixels are tinted, same as the embedded renderers.
            if ( (uRedReal > 220) && (uGreenReal > 220) && (uBlueReal > 220) )
            {
               u32 uNewRed = ((u32)uRedReal * (u32)r) >> 8;
               u32 uNewGreen = ((u32)uGreenReal * (u32)g) >> 8;
               u32 uNewBlue = ((u32)uBlueReal * (u32)b) >> 8;
               if ( uNewRed > 255 ) uNewRed = 255;
               if ( uNewGreen > 255 ) uNewGreen = 255;
               if ( uNewBlue > 255 ) uNewBlue = 255;

               // Re-apply the premultiplication.
               pPixel[0] = (u8)(((u32)uNewBlue * uAlpha) / 255);
               pPixel[1] = (u8)(((u32)uNewGreen * uAlpha) / 255);
               pPixel[2] = (u8)(((u32)uNewRed * uAlpha) / 255);
            }
         }
      }
   }

   cairo_surface_mark_dirty(m_pImages[indexImage]);
}

void RenderEngineDesktop::setVideoFrame(unsigned char* pARGB, int width, int height)
{
   m_pVideoFrameARGB = pARGB;
   m_iVideoFrameWidth = width;
   m_iVideoFrameHeight = height;
}

// Composites the latest video frame into the back buffer, aspect fit and
// centered (letterboxed), before the UI/OSD is drawn on top of it.
void RenderEngineDesktop::_compositeVideoUnderlay()
{
   if ( (NULL == m_pVideoFrameARGB) || (m_iVideoFrameWidth < 1) || (m_iVideoFrameHeight < 1) )
      return;
   if ( (m_iRenderWidth < 1) || (m_iRenderHeight < 1) )
      return;
   if ( ! m_bSDLInitialized )
      return;

   double dScaleW = (double)m_iRenderWidth / (double)m_iVideoFrameWidth;
   double dScaleH = (double)m_iRenderHeight / (double)m_iVideoFrameHeight;
   double dScale = (dScaleW < dScaleH) ? dScaleW : dScaleH;
   int iOutWidth = (int)((double)m_iVideoFrameWidth * dScale + 0.5);
   int iOutHeight = (int)((double)m_iVideoFrameHeight * dScale + 0.5);
   if ( iOutWidth < 1 ) iOutWidth = 1;
   if ( iOutHeight < 1 ) iOutHeight = 1;
   if ( iOutWidth > m_iRenderWidth ) iOutWidth = m_iRenderWidth;
   if ( iOutHeight > m_iRenderHeight ) iOutHeight = m_iRenderHeight;
   int iDstX = (m_iRenderWidth - iOutWidth) / 2;
   int iDstY = (m_iRenderHeight - iOutHeight) / 2;

   u32* pDst = m_pBackBuffer[m_iActiveSurface];
   const u32* pSrc = (const u32*)m_pVideoFrameARGB;

   // The letterbox bars are already black from the clear.
   for ( int y = 0; y < iOutHeight; y++ )
   {
      int iSrcY = (int)(((double)y * (double)m_iVideoFrameHeight) / (double)iOutHeight + 0.5);
      if ( iSrcY >= m_iVideoFrameHeight ) iSrcY = m_iVideoFrameHeight-1;
      const u32* pRowSrc = pSrc + (long)iSrcY * m_iVideoFrameWidth;
      u32* pRowDst = pDst + (long)(iDstY + y) * m_iRenderWidth;
      for ( int x = 0; x < iOutWidth; x++ )
      {
         int iSrcX = (int)(((double)x * (double)m_iVideoFrameWidth) / (double)iOutWidth + 0.5);
         if ( iSrcX >= m_iVideoFrameWidth ) iSrcX = m_iVideoFrameWidth-1;
         pRowDst[iDstX + x] = pRowSrc[iSrcX];
      }
   }
}

void RenderEngineDesktop::startFrame()
{
   if (m_bStartedFrame) return;
   m_bStartedFrame = true;
   m_iRenderDepth++;

   // Switch to back buffer for drawing
   m_iActiveSurface = 1 - m_iActiveSurface;

   // Destroy old context and create new one on the active surface
   if (m_pCairoCtx) cairo_destroy(m_pCairoCtx);
   m_pCairoCtx = cairo_create(m_pMainCairoSurface[m_iActiveSurface]);

   // Clear to transparent/black
   cairo_set_source_rgb(m_pCairoCtx, 0, 0, 0);
   cairo_paint(m_pCairoCtx);

   // Draw the video frame as the underlay, then the UI/OSD is drawn on top.
   _compositeVideoUnderlay();
}

void RenderEngineDesktop::endFrame()
{
   if (!m_bStartedFrame) return;
   m_bStartedFrame = false;
   m_iRenderDepth--;

   // Flush Cairo operations
   if (m_pMainCairoSurface[m_iActiveSurface]) {
      cairo_surface_flush(m_pMainCairoSurface[m_iActiveSurface]);
   }

   // Push the rendered content to the SDL window.
   presentFrame();
}

void RenderEngineDesktop::pollEvents()
{
   _processSDLEvents();
}

void RenderEngineDesktop::_processSDLEvents()
{
   if (NULL == m_pSDLWindow)
      return;


   SDL_Event event;
   while (SDL_PollEvent(&event))
   {
      if (!processEvent(&event))
         break;
   }
}

void RenderEngineDesktop::rotate180()
{
   // Not implemented - would need to swap buffer coordinates
}

cairo_t* RenderEngineDesktop::_createTempDrawContext()
{
   if (m_pCairoTempCtx) cairo_destroy(m_pCairoTempCtx);
   m_pCairoTempCtx = cairo_create(m_pMainCairoSurface[m_iActiveSurface]);
   return m_pCairoTempCtx;
}

cairo_t* RenderEngineDesktop::_getActiveCairoContext()
{
   if (!m_pCairoCtx) {
      m_pCairoCtx = cairo_create(m_pMainCairoSurface[m_iActiveSurface]);
   }
   return m_pCairoCtx;
}

unsigned char* RenderEngineDesktop::_getBackBufferData()
{
   return (unsigned char*)m_pBackBuffer[m_iActiveSurface];
}

int RenderEngineDesktop::_getBackBufferStride()
{
   return m_iRenderWidth * 4; // ARGB32 stride
}

void RenderEngineDesktop::_updateCurrentFontToUse(RenderEngineRawFont* pFont, bool bForce)
{
   cairo_t* pCairoCtx = _getActiveCairoContext();
   if ( NULL == pCairoCtx )
      pCairoCtx = _createTempDrawContext();
   if ( NULL == pCairoCtx )
      return;

   if ( bForce || (s_iLastCairoFontFamilyId != (NULL == pFont ? -1 : pFont->iFamilyId)) || (s_bLastCairoFontStyleBold != ((NULL != pFont) && pFont->bBold)) )
   {
      s_iLastCairoFontFamilyId = (NULL == pFont ? -1 : pFont->iFamilyId);
      s_bLastCairoFontStyleBold = ((NULL != pFont) && pFont->bBold);

      cairo_font_weight_t iStyle = CAIRO_FONT_WEIGHT_NORMAL;
      if ( s_bLastCairoFontStyleBold )
         iStyle = CAIRO_FONT_WEIGHT_BOLD;

      if ( NULL == pFont )
         cairo_select_font_face(pCairoCtx, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      else if ( s_iLastCairoFontFamilyId == 0 )
         cairo_select_font_face(pCairoCtx, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, iStyle);
      else
         cairo_select_font_face(pCairoCtx, "Noto Sans", CAIRO_FONT_SLANT_NORMAL, iStyle);
   }
}

void RenderEngineDesktop::_bltFontChar(int iDestX, int iDestY, int iSrcX, int iSrcY, 
                                        int iSrcWidth, int iSrcHeight, RenderEngineRawFont* pFont)
{
   // Not used on desktop: text is rendered with Cairo's text API in
   // _drawSimpleTextScaled(). This entry point is kept because it is part of
   // the RenderEngine interface used by the shared rendering code.
   (void)iDestX; (void)iDestY; (void)iSrcX; (void)iSrcY;
   (void)iSrcWidth; (void)iSrcHeight; (void)pFont;
}

void RenderEngineDesktop::_blend_pixel(unsigned char* pixel, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
   if (a == 0) return;
   float fa = a / 255.0f;
   pixel[0] = (unsigned char)(pixel[0] * (1 - fa) + r * fa);     // B
   pixel[1] = (unsigned char)(pixel[1] * (1 - fa) + g * fa);     // G  
   pixel[2] = (unsigned char)(pixel[2] * (1 - fa) + b * fa);     // R
}

void RenderEngineDesktop::_draw_hline(int x, int y, int w, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
   if (a == 0 || w <= 0) return;
   
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float fa = a / 255.0f;
   cairo_set_source_rgba(ctx, r/255.0f, g/255.0f, b/255.0f, fa);
   cairo_move_to(ctx, x * m_iRenderWidth, y * m_iRenderHeight);
   cairo_line_to(ctx, (x + w) * m_iRenderWidth, y * m_iRenderHeight);
   cairo_set_line_width(ctx, 1.0);
   cairo_stroke(ctx);
}

void RenderEngineDesktop::_draw_vline(int x, int y, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
   if (a == 0 || h <= 0) return;
   
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float fa = a / 255.0f;
   cairo_set_source_rgba(ctx, r/255.0f, g/255.0f, b/255.0f, fa);
   cairo_move_to(ctx, x * m_iRenderWidth, y * m_iRenderHeight);
   cairo_line_to(ctx, x * m_iRenderWidth, (y + h) * m_iRenderHeight);
   cairo_set_line_width(ctx, 1.0);
   cairo_stroke(ctx);
}

void* RenderEngineDesktop::_loadRawFontImageObject(const char* szFileName)
{
   if ( (NULL == szFileName) || (0 == szFileName[0]) )
      return NULL;

   // The surface must own its pixel data: a surface created over a lodepng
   // buffer that is then freed leaves a dangling pointer (use-after-free).
   cairo_surface_t* surf = cairo_image_surface_create_from_png(szFileName);
   if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      log_softerror_and_alarm("[RenderEngineDesktop] Failed to load font PNG: %s (%s)", szFileName, cairo_status_to_string(cairo_surface_status(surf)));
      cairo_surface_destroy(surf);
      return NULL;
   }

   // Verify the surface is valid by checking its dimensions
   int surfW = cairo_image_surface_get_width(surf);
   int surfH = cairo_image_surface_get_height(surf);
   if (surfW <= 0 || surfH <= 0) {
      log_softerror_and_alarm("[RenderEngineDesktop] Font surface has invalid dimensions: %dx%d", surfW, surfH);
      cairo_surface_destroy(surf);
      return NULL;
   }

   return surf;
}

void RenderEngineDesktop::_freeRawFontImageObject(void* pImageObject)
{
   if (pImageObject) {
      cairo_surface_destroy((cairo_surface_t*)pImageObject);
   }
}

float RenderEngineDesktop::textRawWidthScaled(u32 fontId, float fScale, const char* szText)
{
   if ( (NULL == szText) || (0 == szText[0]) )
      return 0.0;

   RenderEngineRawFont* pFont = _getRawFontFromId(fontId);
   if ( NULL == pFont )
      return 0.0;

   // Measure with the same Cairo font that is used for drawing, so that the
   // measured width matches the rendered width.
   cairo_t* ctx = _getActiveCairoContext();
   if ( NULL == ctx )
      ctx = _createTempDrawContext();
   if ( NULL == ctx )
      return 0.0;

   _updateCurrentFontToUse(pFont, false);

   int iPixels = (int)(pFont->lineHeight * 0.8 * fScale);
   if ( iPixels < 6 )
      iPixels = 6;
   cairo_set_font_size(ctx, iPixels);

   cairo_text_extents_t extents;
   memset(&extents, 0, sizeof(extents));
   cairo_text_extents(ctx, szText, &extents);

   // The extents are in device pixels; convert them to the normalized
   // [0..1] coordinate space used by the rest of the renderer.
   return extents.x_advance * m_fPixelWidth;
}

void RenderEngineDesktop::_drawSimpleText(RenderEngineRawFont* pFont, const char* szText, float xPos, float yPos)
{
   _drawSimpleTextScaled(pFont, szText, xPos, yPos, 1.0f);
}

void RenderEngineDesktop::_drawSimpleTextScaled(RenderEngineRawFont* pFont, const char* szText, float xPos, float yPos, float fScale)
{
   if ( (NULL == pFont) || (NULL == szText) || (0 == szText[0]) )
      return;

   cairo_t* ctx = _getActiveCairoContext();
   if ( NULL == ctx )
      return;

   if ( yPos < 0 )
      return;
   if ( yPos + pFont->lineHeight * fScale * m_fPixelHeight >= 1.0 )
      return;

   char szTxt[256];
   memset(szTxt, 0, sizeof(szTxt));
   strncpy(szTxt, szText, sizeof(szTxt)-1);

   if ( m_bDrawBackgroundBoundingBoxes )
      _drawSimpleTextBoundingBox(pFont, szTxt, xPos, yPos, fScale);

   // Text color: either the strike color of the background bounding box or the current fill color
   float fColor[4];
   if ( m_bDrawBackgroundBoundingBoxes && m_bDrawBackgroundBoundingBoxesTextUsesSameStrokeColor )
   {
      fColor[0] = m_ColorTextBackgroundBoundingBoxStrike[0]/255.0;
      fColor[1] = m_ColorTextBackgroundBoundingBoxStrike[1]/255.0;
      fColor[2] = m_ColorTextBackgroundBoundingBoxStrike[2]/255.0;
      fColor[3] = m_ColorTextBackgroundBoundingBoxStrike[3]/255.0;
   }
   else
   {
      fColor[0] = m_ColorFill[0]/255.0;
      fColor[1] = m_ColorFill[1]/255.0;
      fColor[2] = m_ColorFill[2]/255.0;
      fColor[3] = m_ColorFill[3]/255.0;
   }
   if ( (fColor[3] < 0.25) || (fColor[3] >= 1.0) )
      fColor[3] = 1.0;

   cairo_set_source_rgba(ctx, fColor[0], fColor[1], fColor[2], fColor[3]);

   _updateCurrentFontToUse(pFont, false);

   // The raw font line height (in pixels) drives the Cairo font size so that
   // the rendered text has the same height as on the embedded platforms.
   int iPixels = (int)(pFont->lineHeight * 0.8 * fScale);
   if ( iPixels < 6 )
      iPixels = 6;
   cairo_set_font_size(ctx, iPixels);

   // The baseline of the raw font is expressed in pixels relative to the top
   // of the line; convert it to the Cairo device space. It is already a pixel
   // value, so it must not be scaled by the render height.
   float fBaseline = pFont->baseLine * fScale;
   cairo_move_to(ctx, xPos * m_iRenderWidth, yPos * m_iRenderHeight + fBaseline);

   cairo_show_text(ctx, szTxt);
}

// ============================================================
// Image and icon drawing methods using Cairo
// ============================================================

// Draw a region of a Cairo image surface into a destination rectangle
// expressed in device pixels. The source region is scaled to fit the
// destination rectangle, exactly like the embedded renderers do.
//
// The scaling is done with an explicit pattern matrix. Scaling the Cairo
// context instead would not scale the source pattern: cairo_set_source_surface()
// installs the pattern in the user space that is active at that moment, so the
// image would be zoomed in instead of stretched to the destination rectangle.
void RenderEngineDesktop::_blitSurface(cairo_surface_t* pSurface, int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight,
                                       float fDestX, float fDestY, float fDestWidth, float fDestHeight, float fAlpha)
{
   if ( NULL == pSurface )
      return;

   cairo_t* ctx = _getActiveCairoContext();
   if ( NULL == ctx )
      return;

   int iSurfaceW = cairo_image_surface_get_width(pSurface);
   int iSurfaceH = cairo_image_surface_get_height(pSurface);
   if ( iSurfaceW <= 0 || iSurfaceH <= 0 )
      return;

   if ( iSrcWidth <= 0 ) iSrcWidth = iSurfaceW;
   if ( iSrcHeight <= 0 ) iSrcHeight = iSurfaceH;
   if ( iSrcX < 0 ) iSrcX = 0;
   if ( iSrcY < 0 ) iSrcY = 0;
   if ( iSrcX + iSrcWidth > iSurfaceW ) iSrcWidth = iSurfaceW - iSrcX;
   if ( iSrcY + iSrcHeight > iSurfaceH ) iSrcHeight = iSurfaceH - iSrcY;
   if ( iSrcWidth <= 0 || iSrcHeight <= 0 )
      return;

   if ( fDestWidth <= 0 || fDestHeight <= 0 )
      return;

   // Scale factor that maps device pixels to source pixels.
   double fScaleX = (double)iSrcWidth / (double)fDestWidth;
   double fScaleY = (double)iSrcHeight / (double)fDestHeight;

   cairo_pattern_t* pPattern = cairo_pattern_create_for_surface(pSurface);
   if ( cairo_pattern_status(pPattern) != CAIRO_STATUS_SUCCESS )
   {
      cairo_pattern_destroy(pPattern);
      return;
   }
   cairo_pattern_set_filter(pPattern, CAIRO_FILTER_BILINEAR);
   cairo_pattern_set_extend(pPattern, CAIRO_EXTEND_NONE);

   cairo_matrix_t mMatrix;
   cairo_matrix_init_scale(&mMatrix, fScaleX, fScaleY);
   cairo_matrix_translate(&mMatrix, iSrcX / fScaleX - fDestX, iSrcY / fScaleY - fDestY);
   cairo_pattern_set_matrix(pPattern, &mMatrix);

   cairo_save(ctx);
   cairo_set_source(ctx, pPattern);
   if ( fAlpha < 1.0f )
   {
      // cairo_paint_with_alpha() paints the whole clip region, so the
      // destination rectangle has to be used as the clip.
      cairo_rectangle(ctx, fDestX, fDestY, fDestWidth, fDestHeight);
      cairo_clip(ctx);
      cairo_paint_with_alpha(ctx, fAlpha);
   }
   else
   {
      cairo_rectangle(ctx, fDestX, fDestY, fDestWidth, fDestHeight);
      cairo_fill(ctx);
   }
   cairo_restore(ctx);

   cairo_pattern_destroy(pPattern);
}

// ============================================================
// Shape drawing methods using Cairo
// ============================================================

void RenderEngineDesktop::drawLine(float x1, float y1, float x2, float y2)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   cairo_move_to(ctx, x1 * m_iRenderWidth, y1 * m_iRenderHeight);
   cairo_line_to(ctx, x2 * m_iRenderWidth, y2 * m_iRenderHeight);
   cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
   cairo_stroke(ctx);
}

void RenderEngineDesktop::drawRect(float xPos, float yPos, float fWidth, float fHeight)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float x = xPos * m_iRenderWidth;
   float y = yPos * m_iRenderHeight;
   float w = fWidth * m_iRenderWidth;
   float h = fHeight * m_iRenderHeight;
   if ( w <= 0 || h <= 0 )
      return;

   cairo_rectangle(ctx, x, y, w, h);

   // The embedded renderers draw the filled rectangle and then the border.
   bool bStroke = false;
   if ( m_ColorStroke[3] > 2 )
   if ( m_fStrokeSizePx > 0.00001 )
      bStroke = true;

   if ( m_ColorFill[3] > 2 )
   {
      cairo_set_source_rgba(ctx, m_ColorFill[0]/255.0, m_ColorFill[1]/255.0, m_ColorFill[2]/255.0, m_ColorFill[3]/255.0);
      if ( bStroke )
         cairo_fill_preserve(ctx);
      else
         cairo_fill(ctx);
   }

   if ( bStroke )
   {
      cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
      cairo_stroke(ctx);
   }
}

void RenderEngineDesktop::drawRoundRect(float xPos, float yPos, float fWidth, float fHeight, float fCornerRadius)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float x = xPos * m_iRenderWidth;
   float y = yPos * m_iRenderHeight;
   float w = fWidth * m_iRenderWidth;
   float h = fHeight * m_iRenderHeight;
   float r = fCornerRadius;
   if ( r > w/2 ) r = w/2;
   if ( r > h/2 ) r = h/2;

   cairo_new_sub_path(ctx);
   cairo_arc(ctx, x + w - r, y + r, r, -M_PI/2, 0);
   cairo_arc(ctx, x + w - r, y + h - r, r, 0, M_PI/2);
   cairo_arc(ctx, x + r, y + h - r, r, M_PI/2, M_PI);
   cairo_arc(ctx, x + r, y + r, r, M_PI, M_PI*1.5);
   cairo_close_path(ctx);

   // The embedded renderers draw the filled rectangle and then the border.
   bool bStroke = false;
   if ( m_ColorStroke[3] > 2 )
   if ( m_fStrokeSizePx > 0.00001 )
      bStroke = true;

   if ( m_ColorFill[3] > 2 )
   {
      cairo_set_source_rgba(ctx, m_ColorFill[0]/255.0, m_ColorFill[1]/255.0, m_ColorFill[2]/255.0, m_ColorFill[3]/255.0);
      if ( bStroke )
         cairo_fill_preserve(ctx);
      else
         cairo_fill(ctx);
   }

   if ( bStroke )
   {
      cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
      cairo_stroke(ctx);
   }
}

void RenderEngineDesktop::drawRoundRectMenu(float xPos, float yPos, float fWidth, float fHeight, float fCornerRadius)
{
   drawRoundRect(xPos, yPos, fWidth, fHeight, fCornerRadius);
}

void RenderEngineDesktop::drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   cairo_move_to(ctx, x1 * m_iRenderWidth, y1 * m_iRenderHeight);
   cairo_line_to(ctx, x2 * m_iRenderWidth, y2 * m_iRenderHeight);
   cairo_line_to(ctx, x3 * m_iRenderWidth, y3 * m_iRenderHeight);
   cairo_close_path(ctx);
   cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
   cairo_stroke(ctx);
}

void RenderEngineDesktop::fillTriangle(float x1, float y1, float x2, float y2, float x3, float y3)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   cairo_move_to(ctx, x1 * m_iRenderWidth, y1 * m_iRenderHeight);
   cairo_line_to(ctx, x2 * m_iRenderWidth, y2 * m_iRenderHeight);
   cairo_line_to(ctx, x3 * m_iRenderWidth, y3 * m_iRenderHeight);
   cairo_close_path(ctx);

   bool bStroke = false;
   if ( m_ColorStroke[3] > 2 )
   if ( m_fStrokeSizePx > 0.00001 )
      bStroke = true;

   if ( m_ColorFill[3] > 2 )
   {
      cairo_set_source_rgba(ctx, m_ColorFill[0]/255.0, m_ColorFill[1]/255.0, m_ColorFill[2]/255.0, m_ColorFill[3]/255.0);
      if ( bStroke )
         cairo_fill_preserve(ctx);
      else
         cairo_fill(ctx);
   }

   if ( bStroke )
   {
      cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
      cairo_stroke(ctx);
   }
}

void RenderEngineDesktop::drawPolyLine(float* x, float* y, int count)
{
   if (!x || !y || count < 1) return;
   
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   cairo_move_to(ctx, x[0] * m_iRenderWidth, y[0] * m_iRenderHeight);
   for (int i = 1; i < count; i++) {
      cairo_line_to(ctx, x[i] * m_iRenderWidth, y[i] * m_iRenderHeight);
   }
   cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
   cairo_stroke(ctx);
}

void RenderEngineDesktop::fillPolygon(float* x, float* y, int count)
{
   if (!x || !y || count < 3) return;
   
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   cairo_move_to(ctx, x[0] * m_iRenderWidth, y[0] * m_iRenderHeight);
   for (int i = 1; i < count; i++) {
      cairo_line_to(ctx, x[i] * m_iRenderWidth, y[i] * m_iRenderHeight);
   }
   cairo_close_path(ctx);

   bool bStroke = false;
   if ( m_ColorStroke[3] > 2 )
   if ( m_fStrokeSizePx > 0.00001 )
      bStroke = true;

   if ( m_ColorFill[3] > 2 )
   {
      cairo_set_source_rgba(ctx, m_ColorFill[0]/255.0, m_ColorFill[1]/255.0, m_ColorFill[2]/255.0, m_ColorFill[3]/255.0);
      if ( bStroke )
         cairo_fill_preserve(ctx);
      else
         cairo_fill(ctx);
   }

   if ( bStroke )
   {
      cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
      cairo_stroke(ctx);
   }
}

void RenderEngineDesktop::fillCircle(float x, float y, float r)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float cx = x * m_iRenderWidth;
   float cy = y * m_iRenderHeight;
   float cr = r; // radius in pixels

   cairo_new_sub_path(ctx);
   cairo_arc(ctx, cx, cy, cr, 0, M_PI * 2);
   cairo_set_source_rgba(ctx, m_ColorFill[0]/255.0, m_ColorFill[1]/255.0, m_ColorFill[2]/255.0, m_ColorFill[3]/255.0);
   cairo_fill(ctx);
}

void RenderEngineDesktop::drawCircle(float x, float y, float r)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float cx = x * m_iRenderWidth;
   float cy = y * m_iRenderHeight;
   float cr = r;

   cairo_new_sub_path(ctx);
   cairo_arc(ctx, cx, cy, cr, 0, M_PI * 2);
   cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
   cairo_stroke(ctx);
}

void RenderEngineDesktop::drawArc(float x, float y, float r, float a1, float a2)
{
   cairo_t* ctx = _getActiveCairoContext();
   if (!ctx) return;

   float cx = x * m_iRenderWidth;
   float cy = y * m_iRenderHeight;

   cairo_new_sub_path(ctx);
   cairo_arc(ctx, cx, cy, r, a1, a2);
   cairo_set_source_rgba(ctx, m_ColorStroke[0]/255.0, m_ColorStroke[1]/255.0, m_ColorStroke[2]/255.0, m_ColorStroke[3]/255.0);
   cairo_stroke(ctx);
}

void RenderEngineDesktop::drawImage(float xPos, float yPos, float fWidth, float fHeight, u32 uImageId)
{
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_ImageIds[i] == uImageId && m_pImages[i]) {
         _blitSurface(m_pImages[i], 0, 0, 0, 0,
                      xPos * m_iRenderWidth, yPos * m_iRenderHeight,
                      fWidth * m_iRenderWidth, fHeight * m_iRenderHeight, 1.0f);
         return;
      }
   }
}

void RenderEngineDesktop::drawImageAlpha(float xPos, float yPos, float fWidth, float fHeight, u32 uImageId, u8 uAlpha)
{
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_ImageIds[i] == uImageId && m_pImages[i]) {
         _blitSurface(m_pImages[i], 0, 0, 0, 0,
                      xPos * m_iRenderWidth, yPos * m_iRenderHeight,
                      fWidth * m_iRenderWidth, fHeight * m_iRenderHeight, (float)uAlpha / 255.0f);
         return;
      }
   }
}

void RenderEngineDesktop::bltImage(float xPosDest, float yPosDest, float fWidthDest, float fHeightDest, 
                                   int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight, u32 uImageId)
{
   for (int i = 0; i < m_iCountImages; i++) {
      if (m_ImageIds[i] == uImageId && m_pImages[i]) {
         _blitSurface(m_pImages[i], iSrcX, iSrcY, iSrcWidth, iSrcHeight,
                      xPosDest * m_iRenderWidth, yPosDest * m_iRenderHeight,
                      fWidthDest * m_iRenderWidth, fHeightDest * m_iRenderHeight, 1.0f);
         return;
      }
   }
}

void RenderEngineDesktop::bltSprite(float xPosDest, float yPosDest, int iSrcX, int iSrcY, 
                                     int iSrcWidth, int iSrcHeight, u32 uImageId)
{
   // Blit a region of the image, without scaling it.
   // Same as RenderEngineRaw::bltSprite: the destination size must be the
   // source sprite pixel size (36x54 or 24x36 pixels for the MSP OSD font
   // glyph cells). bltImage() takes the destination size as a fraction of the
   // render surface, so the pixel size must be converted to normalized units.
   // Passing the raw pixel values made each sprite occupy 36 times the render
   // surface width: a giant solid rectangle painted over the video, instead
   // of the OSD.
   bltImage(xPosDest, yPosDest,
      (float)iSrcWidth / (float)m_iRenderWidth, (float)iSrcHeight / (float)m_iRenderHeight,
      iSrcX, iSrcY, iSrcWidth, iSrcHeight, uImageId);
}

void RenderEngineDesktop::drawIcon(float xPos, float yPos, float fWidth, float fHeight, u32 uIconId)
{
   for (int i = 0; i < m_iCountIcons; i++) {
      if (m_IconIds[i] == uIconId && m_pIcons[i]) {
         _blitSurface(m_pIcons[i], 0, 0, 0, 0,
                      xPos * m_iRenderWidth, yPos * m_iRenderHeight,
                      fWidth * m_iRenderWidth, fHeight * m_iRenderHeight, 1.0f);
         return;
      }
   }
}

void RenderEngineDesktop::bltIcon(float xPosDest, float yPosDest, int iSrcX, int iSrcY, 
                                  int iSrcWidth, int iSrcHeight, u32 uIconId)
{
   // Blit a region of the icon, without scaling it.
   for (int i = 0; i < m_iCountIcons; i++) {
      if (m_IconIds[i] == uIconId && m_pIcons[i]) {
         _blitSurface(m_pIcons[i], iSrcX, iSrcY, iSrcWidth, iSrcHeight,
                      xPosDest * m_iRenderWidth, yPosDest * m_iRenderHeight,
                      (float)iSrcWidth, (float)iSrcHeight, 1.0f);
         return;
      }
   }
}
