/* GdkQuartzView.h
 *
 * Copyright (C) 2005 Imendio AB
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#import <AppKit/AppKit.h>
#include "gdkwindow.h"

@interface GdkQuartzView : NSView {
  GdkWindow *gdk_window;
  NSTrackingRectTag trackingRect;
  BOOL needsInvalidateShadow;
}

-(void)setGdkWindow:(GdkWindow *)window;
-(GdkWindow *)gdkWindow;
-(NSTrackingRectTag)trackingRect;
-(void)setNeedsInvalidateShadow:(BOOL)invalidate;

@end
