#pragma once
/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "utils/CriticalSection.h"
#include "gui3d.h"
#include "StdString.h"

typedef uint32_t color_t;

class CBaseTexture;

class CSlideShowPic
{
public:
  enum DISPLAY_EFFECT { EFFECT_NONE = 0, EFFECT_FLOAT, EFFECT_ZOOM, EFFECT_RANDOM, EFFECT_NO_TIMEOUT };
  enum TRANSISTION_EFFECT { TRANSISTION_NONE = 0, FADEIN_FADEOUT, CROSSFADE, TRANSISTION_ZOOM, TRANSISTION_ROTATE };

  struct TRANSISTION
  {
    TRANSISTION_EFFECT type;
    int start;
    int length;
  };

  CSlideShowPic();
  ~CSlideShowPic();

  void SetTexture(int iSlideNumber, CBaseTexture* pTexture, DISPLAY_EFFECT dispEffect = EFFECT_RANDOM, TRANSISTION_EFFECT transEffect = FADEIN_FADEOUT);
  void UpdateTexture(CBaseTexture* pTexture);

  bool IsLoaded() const { return m_bIsLoaded;};
  void UnLoad() {m_bIsLoaded = false;};
  void Render();
  void Close();
  bool IsFinished() const { return m_bIsFinished;};
  bool DrawNextImage() const { return m_bDrawNextImage;};

  int GetWidth() const { return (int)m_fWidth;};
  int GetHeight() const { return (int)m_fHeight;};

  void Keep();
  bool StartTransistion();
  int GetTransistionTime(int iType) const;
  void SetTransistionTime(int iType, int iTime);

  int SlideNumber() const { return m_iSlideNumber;};
  void EnableBlend(bool enable) { m_bBlend = enable; }

  void Zoom(int iZoomAmount, bool immediate = false);
  void Rotate(int iRotateAmount);
  void Pause(bool bPause);
  void SetInSlideshow(bool slideshow);
  void SetOriginalSize(int iOriginalWidth, int iOriginalHeight, bool bFullSize);
  bool FullSize() const { return m_bFullSize;};
  int GetOriginalWidth();
  int GetOriginalHeight();

  void Move(float dX, float dY);
  float GetZoom() const { return m_fZoomAmount;};
  
  void SetScreenSaverMode(bool bMode) { m_bScreenSaverMode = bMode; }
  void CopyLocationAndZoom(const CSlideShowPic& pic);
  
  bool m_bIsComic;
protected:
  void Process();
  void Render(float *x, float *y, CBaseTexture* pTexture, color_t color);

private:
  CBaseTexture *m_pImage;

  int m_iOriginalWidth;
  int m_iOriginalHeight;
  int m_iSlideNumber;
  bool m_bIsLoaded;
  bool m_bIsFinished;
  bool m_bDrawNextImage;
  CStdString m_strFileName;
  float m_fWidth;
  float m_fHeight;
  color_t m_alpha;
  // stuff relative to middle position
  float m_fPosX;
  float m_fPosY;
  float m_fPosZ;
  float m_fVelocityX;
  float m_fVelocityY;
  float m_fVelocityZ;
  float m_fZoomAmount;
  float m_fZoomLeft;
  float m_fZoomTop;
  // transistion and display effects
  DISPLAY_EFFECT m_displayEffect;
  TRANSISTION m_transistionStart;
  TRANSISTION m_transistionEnd;
  TRANSISTION m_transistionTemp; // used for rotations + zooms
  float m_fAngle; // angle (between 0 and 2pi to display the image)
  float m_fTransistionAngle;
  float m_fTransistionZoom;
  int m_iCounter;
  int m_iTotalFrames;
  bool m_bPause;
  bool m_bNoEffect;
  bool m_bFullSize;
  bool m_bTransistionImmediately;
  bool m_bBlend;
#if defined(HAS_GLES) || defined(HAS_GL)
  GLint m_uniMatModelView;
  GLint m_uniMatProjection;

  GLint m_uniTexture0;
  GLint m_uniColor;
  GLint m_attribVertex;
  GLint m_attribTextureCoord0;
#endif
  bool m_bScreenSaverMode;

  CCriticalSection m_textureAccess;
};
