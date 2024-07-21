#ifndef NVXJITTER_H_
#define NVXJITTER_H_

#include <gst/gst.h>
#include <stdio.h>

typedef struct GstEglJitterTool
{
    gchar *pName;
    guint64 *pTicks;
    guint nTicksMax;
    guint nTickCount;

    guint64 nLastTime;

    gboolean bShow;

#define MAX_JITTER_HISTORY 3000
    double fAvg[MAX_JITTER_HISTORY];
    double fStdDev[MAX_JITTER_HISTORY];
    guint nPos;
} GstEglJitterTool;

GstEglJitterTool *GstEglAllocJitterTool(const char *pName, guint nTicks);
void GstEglFreeJitterTool(GstEglJitterTool *pTool);

void GstEglJitterToolAddPoint(GstEglJitterTool *pTool);
void GstEglJitterToolSetShow(GstEglJitterTool *pTool, gboolean bShow);

void GstEglJitterToolGetAvgs(GstEglJitterTool *pTool, double *pStdDev, double *pAvg,
                          double *pHighest);

#endif

