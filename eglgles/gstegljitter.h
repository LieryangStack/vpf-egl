#ifndef NVXJITTER_H_
#define NVXJITTER_H_

#include <gst/gst.h>
#include <stdio.h>

typedef struct GstEglJitterTool
{
    gchar *pName; /*名称，用于标识或描述这个抖动工具实例 */
    guint64 *pTicks; /* 存储时间戳数组 */
    guint nTicksMax; /* pTicks 数组的最大大小 */
    guint nTickCount; /* 当前时间戳的数量 */

    guint64 nLastTime; /* 最后一个时间戳，用于计算时间间隔 */

    gboolean bShow; /* 标志，指示是否显示抖动信息 */

#define MAX_JITTER_HISTORY 3000
    double fAvg[MAX_JITTER_HISTORY]; /* 存储抖动的平均值历史记录 */
    double fStdDev[MAX_JITTER_HISTORY]; /* 存储抖动的标准差历史记录 */
    guint nPos; /* 当前在历史记录数组中的位置 */
} GstEglJitterTool;


GstEglJitterTool *GstEglAllocJitterTool(const char *pName, guint nTicks);
void GstEglFreeJitterTool(GstEglJitterTool *pTool);

void GstEglJitterToolAddPoint(GstEglJitterTool *pTool);
void GstEglJitterToolSetShow(GstEglJitterTool *pTool, gboolean bShow);

void GstEglJitterToolGetAvgs(GstEglJitterTool *pTool, double *pStdDev, double *pAvg,
                          double *pHighest);

#endif

