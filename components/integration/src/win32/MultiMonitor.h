/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
// 
// Software distributed under the License is distributed 
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either 
// express or implied. See the GPL for the specific language 
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this 
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc., 
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
// 
// END SONGBIRD GPL
//
 */

/** 
* \file  MultiMonitor.h
* \brief Songbird Multiple Monitor Support - Definition.
*/

#ifndef __MULTI_MONITOR_H__
#define __MULTI_MONITOR_H__

#ifdef XP_WIN
#include <windows.h>
#else
typedef long LONG;

typedef struct tagPOINT
{
  LONG x;
  LONG y;
} POINT;

typedef struct tagRECT
{
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT;

#endif

#include "../NativeWindowFromNode.h" // for NATIVEWINDOW type

// INCLUDES ===================================================================

// CLASSES ====================================================================

class CMultiMonitor
{
public:
  static void GetMonitorFromPoint(RECT *r, POINT *pt, bool excludeTaskbar);
  static void GetMonitorFromWindow(RECT *r, NATIVEWINDOW wnd, bool excludeTaskbar);
};

#endif // __MULTI_MONITOR_H__

