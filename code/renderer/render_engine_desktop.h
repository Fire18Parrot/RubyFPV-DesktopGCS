#pragma once

#include "render_engine.h"
#include <cairo.h>
#include <SDL2/SDL.h>

class RenderEngineDesktop: public RenderEngine
{
   public:
     RenderEngineDesktop();
     virtual ~RenderEngineDesktop();

     // Override base class initEngine to initialize the window
     virtual bool initEngine();
     
     // Initialize SDL2 window and Cairo surfaces. Returns 0 on success, -1 on failure.
     int initWindow(int width, int height);
     
     // Process one SDL event (call from main loop). Returns false when quit requested.
     bool processEvent(SDL_Event* event);
     
     // Present the current frame to screen
     void presentFrame();

     // Poll and dispatch the pending SDL events
     void _processSDLEvents();

     virtual void* getDrawContext();
     virtual void setStroke(const double* color, float fStrokeSize); 
     virtual void setStrokeSize(float fStrokeSize);

     virtual void setFontOutlineColor(u32 idFont, u8 r, u8 g, u8 b, u8 a);
     virtual u32 loadImage(const char* szFile);
     virtual void freeImage(u32 idImage);
     virtual u32 loadIcon(const char* szFile);
     virtual void freeIcon(u32 idIcon);
     virtual int getImageWidth(u32 uImageId);
     virtual int getImageHeight(u32 uImageId);
     virtual void changeImageHue(u32 uImageId, u8 r, u8 g, u8 b);
     
     virtual void startFrame();
     virtual void endFrame();
     virtual void pollEvents();
     virtual void rotate180();

     virtual void drawImage(float xPos, float yPos, float fWidth, float fHeight, u32 uImageId);
     virtual void drawImageAlpha(float xPos, float yPos, float fWidth, float fHeight, u32 uImageId, u8 uAlpha);
     virtual void bltImage(float xPosDest, float yPosDest, float fWidthDest, float fHeightDest, int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight, u32 uImageId);
     virtual void bltSprite(float xPosDest, float yPosDest, int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight, u32 uImageId);
     virtual void drawIcon(float xPos, float yPos, float fWidth, float fHeight, u32 uIconId);
     virtual void bltIcon(float xPosDest, float yPosDest, int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight, u32 uIconId);

     virtual float textRawWidthScaled(u32 fontId, float fScale, const char* szText);

     virtual void drawLine(float x1, float y1, float x2, float y2);
     virtual void drawRect(float xPos, float yPos, float fWidth, float fHeight);
     virtual void drawRoundRect(float xPos, float yPos, float fWidth, float fHeight, float fCornerRadius);
     virtual void drawRoundRectMenu(float xPos, float yPos, float fWidth, float fHeight, float fCornerRadius);
     virtual void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3);
     virtual void fillTriangle(float x1, float y1, float x2, float y2, float x3, float y3);
     virtual void drawPolyLine(float* x, float* y, int count);
     virtual void fillPolygon(float* x, float* y, int count);

     virtual void fillCircle(float x, float y, float r);
     virtual void drawCircle(float x, float y, float r);
     virtual void drawArc(float x, float y, float r, float a1, float a2);
     
   protected:
      cairo_t* _createTempDrawContext();
      cairo_t* _getActiveCairoContext();

      // Draw a source rectangle of a Cairo surface into a destination
      // rectangle (both in device pixels), scaling the source as needed.
      void _blitSurface(cairo_surface_t* pSurface, int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight,
                        float fDestX, float fDestY, float fDestWidth, float fDestHeight, float fAlpha);
      virtual void* _loadRawFontImageObject(const char* szFileName);
      virtual void _freeRawFontImageObject(void* pImageObject);

      void _updateCurrentFontToUse(RenderEngineRawFont* pFont, bool bForce);
      virtual void _drawSimpleText(RenderEngineRawFont* pFont, const char* szText, float xPos, float yPos);
      virtual void _drawSimpleTextScaled(RenderEngineRawFont* pFont, const char* szText, float xPos, float yPos, float fScale);
      void _bltFontChar(int iDestX, int iDestY, int iSrcX, int iSrcY, int iSrcWidth, int iSrcHeight, RenderEngineRawFont* pFont);
      void _blend_pixel(unsigned char* pixel, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
      void _draw_hline(int x, int y, int w, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
      void _draw_vline(int x, int y, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a);

      // Get the current back buffer data pointer and stride for Cairo surface creation
      unsigned char* _getBackBufferData();
      int _getBackBufferStride();

      bool m_bUseDoubleBuffering;
      u32 m_uRenderDrawSurfacesIds[2];
      cairo_surface_t *m_pMainCairoSurface[2];
      cairo_t* m_pCairoCtx;
      cairo_t* m_pCairoTempCtx;
      bool m_bMustTestFontAccess;
      bool m_bHasNewFont;

      // SDL2 state
      bool           m_bSDLInitialized;
      SDL_Window*    m_pSDLWindow;
      SDL_Renderer*  m_pSDLSurface;
      SDL_Texture*   m_pSDLTexture;
      Uint32*        m_pBackBuffer[2];   // Raw pixel buffers (double-buffered)
      int            m_iActiveSurface;
      
      cairo_surface_t* m_pImages[MAX_RAW_IMAGES];
      u32              m_ImageIds[MAX_RAW_IMAGES];
      u32              m_CurrentImageId;
      int              m_iCountImages;

      cairo_surface_t* m_pIcons[MAX_RAW_ICONS];
      cairo_surface_t* m_pIconsMip[MAX_RAW_ICONS][2];
      u32              m_IconIds[MAX_RAW_ICONS];
      u32              m_CurrentIconId;
      int              m_iCountIcons;

};
