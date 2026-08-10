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
//
// Every name this header exports begins with IccMax, and nothing sits in the library's own
// cms namespace: this may ship as a package of its own, and a plug-in has no business
// claiming names that a future version of Little-CMS might want.

#ifndef _IccMaxPlugin_H
#define _IccMaxPlugin_H

#include "lcms2_plugin.h"

// Signatures this plug-in adds. They are defined here rather than in lcms2.h precisely
// because nothing in the library needs to know them: a plug-in brings its own.

// extendedCLUTElement, ICC.2:2023 11.2.7 Table 117
#define IccMaxSigExtCLutElemType     ((cmsStageSignature)   0x78636C74)  // 'xclt'

// The tag type holding an embedded ICC.2 profile, and the tag that carries it, from the
// ICC technical note "Embedding an ICC.2 (iccMAX) profile in an ICC.1 profile"
#define IccMaxSigEmbeddedProfileType ((cmsTagTypeSignature) 0x49434370)  // 'ICCp'
#define IccMaxSigEmbeddedV5Tag       ((cmsTagSignature)     0x49434335)  // 'ICC5'

// Wrap a saved ICC.2 profile image as the payload of an 'ICC5' tag on an ICC.1 profile, and
// recover it. Both work through the registered ICC5 tag and its 'ICCp' tag type, so neither
// caller sees the 8 byte type-signature-plus-reserved prefix that the tag content carries on
// disk -- the framework consumes it. IccMaxEmbedProfile copies the bytes; cmsWriteTag never
// takes ownership, so the caller's buffer stays the caller's.
//
// IccMaxExtractProfile allocates *SubProfile with _cmsMalloc against hOuter's context, and the
// caller owns it: release it with _cmsFree(cmsGetProfileContextID(hOuter), *SubProfile). It is
// a copy of the tag object's bytes, not the object's own storage, so it outlives hOuter. Both
// out-parameters are required, unlike the optional ones on the header accessors below: a buffer
// without its length is of no use to anyone.
CMSAPI cmsBool CMSEXPORT IccMaxEmbedProfile(cmsHPROFILE hOuter, const void* SubProfile,
                                             cmsUInt32Number Size);
CMSAPI cmsBool CMSEXPORT IccMaxExtractProfile(cmsHPROFILE hOuter, void** SubProfile,
                                               cmsUInt32Number* Size);

// The lcms parametric curve types this plug-in defines, being ICC.2 formulaCurveSegment
// function types 3 to 7 under the "lcms type = ICC type + 6" convention core already uses
// for ICC types 0, 1 and 2.
#define IccMaxFirstCurveType         9
#define IccMaxLastCurveType          13

// float16ArrayType and float32ArrayType, ICC.2:2023 10.2.9 and 10.2.10, and the tag that
// carries the spectral white point in either of them (or in the core's own uInt16ArrayType).
#define IccMaxSigFloat16ArrayType        ((cmsTagTypeSignature) 0x666C3136)  // 'fl16'
#define IccMaxSigFloat32ArrayType        ((cmsTagTypeSignature) 0x666C3332)  // 'fl32'
#define IccMaxSigSpectralWhitePointTag   ((cmsTagSignature)     0x73777074)  // 'swpt', ICC.2 9.2.112

// spectralViewingConditionsType, ICC.2:2023 10.2.22 (Table 69), and the tag that carries it,
// ICC.2:2023 9.2.111. ICC.2 reuses the 'svcn' FourCC for both the tag signature and the type
// signature: the tag directory entry is followed by a nonzero file offset, while the type
// header at the start of the tag's own data is followed by 4 reserved zero bytes.
#define IccMaxSigSpectralViewingConditionsType ((cmsTagTypeSignature) 0x7376636E)  // 'svcn'
#define IccMaxSigSpectralViewingConditionsTag  ((cmsTagSignature)     0x7376636E)  // 'svcn'

// A bare vector of values, sized by nValues rather than by any fixed per-tag constant. See
// iccmax_plugin.c for why: TagDescriptor->ElemCount cannot express this tag's length.
typedef struct {

    cmsContext        ContextID;
    cmsUInt32Number    nValues;
    cmsFloat32Number*  Values;

} IccMaxFloatArray;

// Allocates a zeroed value array. nValues is capped at 0xFFFF, the largest channel count an
// ICC.2 spectral PCS signature can express (its channel count is a 16 bit field).
CMSAPI IccMaxFloatArray* CMSEXPORT IccMaxAllocFloatArray(cmsContext ContextID, cmsUInt32Number nValues);
CMSAPI void              CMSEXPORT IccMaxFreeFloatArray(IccMaxFloatArray* v);

// swpt accessors, working directly over cmsReadRawTag / cmsWriteRawTag so that they can also
// reach the core's own uInt16ArrayType encoding, which is not registered as a handler here.
// IccMaxReadSpectralWhitePoint allocates *Out; release it with IccMaxFreeFloatArray.
CMSAPI cmsBool           CMSEXPORT IccMaxReadSpectralWhitePoint(cmsHPROFILE hProfile,
                                                                 IccMaxFloatArray** Out);
CMSAPI cmsBool           CMSEXPORT IccMaxWriteSpectralWhitePoint(cmsHPROFILE hProfile,
                                                                  const IccMaxFloatArray* In,
                                                                  cmsTagTypeSignature AsType);

// Observer and illuminant for a spectrally-based PCS (ICC.2:2023 Table 69, as corrected on
// 2026-08-07: the two trailing CIEXYZ triples are float32Number[3], not XYZNumber). The
// observer step count N and the illuminant step count M are independent of each other and of
// the spectral PCS channel count -- nothing here may assume they agree.
typedef struct {

    cmsContext        ContextID;

    cmsUInt32Number    ObserverType;      // Table 70: 0 custom, 1 CIE 1931, 2 CIE 1964
    cmsFloat32Number   ObserverStart;     // nm
    cmsFloat32Number   ObserverEnd;       // nm
    cmsUInt16Number    ObserverSteps;     // N
    cmsFloat32Number*  Observer;          // 3N values: all X, then all Y, then all Z

    cmsUInt32Number    IlluminantType;    // Table 71: 1 D50, 9 black body by CCT, ...
    cmsFloat32Number   CCT;               // only meaningful for the black-body types
    cmsFloat32Number   IlluminantStart;   // nm
    cmsFloat32Number   IlluminantEnd;     // nm
    cmsUInt16Number    IlluminantSteps;   // M
    cmsFloat32Number*  Illuminant;        // M values

    cmsCIEXYZ          IlluminantXYZ;     // un-normalised, Y in cd/m2
    cmsCIEXYZ          SurroundXYZ;       // un-normalised

} IccMaxSpectralViewingConditions;

// Allocates a zeroed svcn payload with N observer steps and M illuminant steps.
CMSAPI IccMaxSpectralViewingConditions* CMSEXPORT IccMaxAllocSpectralViewingConditions(
    cmsContext ContextID, cmsUInt16Number ObserverSteps, cmsUInt16Number IlluminantSteps);
CMSAPI void                             CMSEXPORT IccMaxFreeSpectralViewingConditions(
    IccMaxSpectralViewingConditions* v);

// Spectral PCS fields from an ICC.2 profile header, bytes 100..109 (ICC.2:2023 7.2.1). No
// plug-in hook reaches the header -- plug-ins cannot hook header parsing, nor add fields to
// _cmsICCPROFILE -- so these operate on a profile image in memory instead of a cmsHPROFILE.
// That is not a workaround: the sub-profile already exists that way on both sides of the
// hybrid-printer use case. On read, it arrives as bytes out of the containing profile's ICC5
// tag; on write, cmsSaveProfileToMem produces exactly this kind of buffer, and the caller
// patches bytes 100..109 before wrapping the result back into an ICC5 tag.
//
// Both validate Size >= 128 before touching anything. Every output pointer is optional (NULL
// means "don't want it"), matching the convention cmsGetSpectralPCSRange uses on the other
// branch. Set refuses a profile whose header major version is below 5: those bytes are
// reserved in ICC.1, and writing them would corrupt a v4 profile. Encoding is big-endian
// throughout: PCS as uInt32 at 100..103, start and end as float16 at 104..107, steps as
// uInt16 at 108..109.
CMSAPI cmsBool CMSEXPORT IccMaxGetSpectralPCSFromMem(const void* Profile, cmsUInt32Number Size,
                                                      cmsUInt32Number* PCS, cmsFloat32Number* Start,
                                                      cmsFloat32Number* End, cmsUInt16Number* Steps);

CMSAPI cmsBool CMSEXPORT IccMaxSetSpectralPCSInMem(void* Profile, cmsUInt32Number Size,
                                                    cmsUInt32Number PCS, cmsFloat32Number Start,
                                                    cmsFloat32Number End, cmsUInt16Number Steps);

// Returns the head of a chained plug-in list registering all of the above. Hand it to
// cmsPlugin or cmsPluginTHR. The list is static, so there is nothing to free.
CMSAPI cmsPluginBase* CMSEXPORT IccMaxGetPlugin(void);

#endif
