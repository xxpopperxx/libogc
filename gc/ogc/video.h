#ifndef __VIDEO_H__
#define __VIDEO_H__

#include <gctypes.h>
#include "gx_struct.h"
#include "video_types.h"

#define VIDEO_PadFramebufferWidth(width)     ((u16)(((u16)(width) + 15) & ~15))

#ifdef __cplusplus
   extern "C" {
#endif /* __cplusplus */

extern GXRModeObj TVNtsc240Ds; 
extern GXRModeObj TVNtsc240DsAa;
extern GXRModeObj TVNtsc240Int; 
extern GXRModeObj TVNtsc240IntAa;
extern GXRModeObj TVNtsc480IntDf;
extern GXRModeObj TVNtsc480IntAa;
extern GXRModeObj TVMpal480IntDf;
extern GXRModeObj TVPal264Ds;
extern GXRModeObj TVPal264DsAa;
extern GXRModeObj TVPal264Int;
extern GXRModeObj TVPal264IntAa;
extern GXRModeObj TVPal524IntAa;
extern GXRModeObj TVPal528Int;
extern GXRModeObj TVPal528IntDf;
extern GXRModeObj TVPal574IntDfScale;

typedef void (*VIRetraceCallback)(u32);

void VIDEO_Init();
void VIDEO_Flush();
void VIDEO_SetBlack(boolean black);
u32 VIDEO_GetNextField();
u32 VIDEO_GetCurrentLine();
u32 VIDEO_GetCurrentTvMode();
void VIDEO_Configure(GXRModeObj *rmode);
void VIDEO_ClearFrameBuffer(GXRModeObj *rmode,void *fb,u32 color);
void VIDEO_WaitVSync(void);
void VIDEO_SetNextFramebuffer(void *fb);
void VIDEO_SetNextRightFramebuffer(void *fb);
VIRetraceCallback VIDEO_SetPreRetraceCallback(VIRetraceCallback callback);
VIRetraceCallback VIDEO_SetPostRetraceCallback(VIRetraceCallback callback);

#ifdef __cplusplus
   }
#endif /* __cplusplus */

#endif
