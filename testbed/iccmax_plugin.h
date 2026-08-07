//---------------------------------------------------------------------------------
//
//  Little Color Management System
//  Copyright (c) 1998-2026 Marti Maria Saguer
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//---------------------------------------------------------------------------------
//
// A demonstration plug-in that adds enough iccMAX (ICC.2) reading for the spectral
// transform of a hybrid ICC.1 / ICC.2 printer profile to be evaluated. Everything here
// is purely additive: no signature registered below is one Little-CMS already handles.
//
// See iccmax_plugin.c for the detail.

#ifndef _ICCMAX_PLUGIN_H
#define _ICCMAX_PLUGIN_H

#include "lcms2_plugin.h"

// Signatures this plug-in adds. They are defined here rather than in lcms2.h precisely
// because nothing in the library needs to know them: a plug-in brings its own.

// extendedCLUTElement, ICC.2:2023 11.2.7 Table 117
#define ICCMAX_SigExtCLutElemType     ((cmsStageSignature)   0x78636C74)  // 'xclt'

// The tag type holding an embedded ICC.2 profile, and the tag that carries it, from the
// ICC technical note "Embedding an ICC.2 (iccMAX) profile in an ICC.1 profile"
#define ICCMAX_SigEmbeddedProfileType ((cmsTagTypeSignature) 0x49434370)  // 'ICCp'
#define ICCMAX_SigEmbeddedV5Tag       ((cmsTagSignature)     0x49434335)  // 'ICC5'

// The lcms parametric curve types this plug-in defines, being ICC.2 formulaCurveSegment
// function types 3 to 7 under the "lcms type = ICC type + 6" convention core already uses
// for ICC types 0, 1 and 2.
#define ICCMAX_FirstCurveType         9
#define ICCMAX_LastCurveType          13

// Returns the head of a chained plug-in list registering all of the above. Hand it to
// cmsPlugin or cmsPluginTHR. The list is static, so there is nothing to free.
CMSAPI cmsPluginBase* CMSEXPORT cmsGetIccMaxPlugin(void);

#endif
