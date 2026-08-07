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
// iccMAX (ICC.2) spectral reading, as a plug-in.
//
// This exists to answer one question: can the iccMAX hybrid-printer feature be delivered
// without duplicating or overriding any part of Little-CMS? Everything below hangs off
// hooks the plug-in API already provides, and every signature registered is one the
// library does not handle:
//
//   'xclt'  extendedCLUTElement, a new multi process element type
//   'ICCp'  embeddedProfileType, a new tag type
//   'ICC5'  the tag that carries it, a new tag
//   lcms parametric curve types 9 to 13, being ICC.2 formulaCurveSegment function
//           types 3 to 7, which ICC.1 does not define
//
// Nothing here re-implements 'cvst', 'clut', 'matf', 'mpet', 'curf', 'parf', 'samf' or
// 'sngf'. Those are read by the library, and this plug-in inherits every fix made to them.
//
// Three things in the library make that possible, and none of them mention iccMAX:
//
//   A. Multi process element pipelines may be as wide as MAX_STAGE_CHANNELS, which is what
//      the pipeline evaluators were always written for. The fixture's transform is 4 -> 36.
//   B. The segmented curve reader and writer ask the parametric curve collections how many
//      parameters a formula type takes, instead of carrying a private table of the three
//      ICC.1 types. That is what makes the curve types below readable from a profile, and
//      it closes a pre-existing asymmetry: before it, a registered curve type could be
//      evaluated but never serialized.
//   C. singleSampledCurve ('sngf') is read by the library, since it is an alternative
//      encoding of a curve inside the standard 'cvst' element rather than a new element.

#include "lcms2_internal.h"
#include "iccmax_plugin.h"


// ********************************************************************************
// extendedCLUTElement ('xclt') -- ICC.2:2023 11.2.7, Table 117
// ********************************************************************************
//
// Same grid layout and interpolation as the ICC.1 clutElement, but the samples may be
// encoded as float32Number, float16Number, uInt16Number or uInt8Number, selected by a
// valueEncodingType field (ICC.2:2023 4.2.10, Table 8).
//
//     0..3   'xclt' signature       ] consumed by the library's ReadMPEElem, which reads
//     4..6   reserved, shall be 0   ] the signature plus 4 bytes, so this handler starts
//     7      interpolation hint     ] at byte 8. The hint is advisory and not retained.
//     8..9   input channels  (P)    uInt16Number
//     10..11 output channels (Q)    uInt16Number
//     12..13 CLUT encoding type     uInt16Number as valueEncodingType
//     14..15 reserved, shall be 0
//     16..31 grid points per channel uInt8Number[16]
//     32..   CLUT data              per encoding type
//
// Table 117 describes bytes 12 to 15 as a single uInt32Number, but 4.2.10 permits a
// valueEncodingType to be encoded as either a uInt16Number or a uInt32Number, and real
// iccMAX profiles put a uInt16Number in bytes 12 and 13 followed by two reserved bytes. So
// does the reference implementation, CIccMpeExtCLUT::Read, which does Read16(storageType)
// then Read16(reserved). Both spellings are accepted here, which is unambiguous: the
// uInt16 form always leaves bytes 14 and 15 zero, and the two only coincide for
// float32Number, where either way all four bytes are zero.
//
// Table 117 permits P up to 16, but MAX_INPUT_DIMENSIONS is 15, so 16 is rejected rather
// than silently truncated. That is the same bound the library's own 'clut' reader carries.

// Bytes of the element consumed before the CLUT data starts: the 'xclt' signature and the
// reserved word read by ReadMPEElem, plus Table 117's own 24 byte header read below.
#define EXTCLUT_HEADER_BYTES  32

static
void* Type_MPEextclut_Read(struct _cms_typehandler_struct* self, cmsIOHANDLER* io,
                           cmsUInt32Number* nItems, cmsUInt32Number SizeOfTag)
{
    cmsStage* mpe = NULL;
    cmsUInt16Number InputChans, OutputChans;
    cmsUInt8Number Dimensions8[16];
    cmsUInt32Number i, nMaxGrids, GridPoints[MAX_INPUT_DIMENSIONS];
    cmsUInt16Number EncodingType, Reserved2;
    cmsUInt32Number nEntries, BytesPerSample;
    _cmsStageCLutData* clut;

    *nItems = 0;

    if (!_cmsReadUInt16Number(io, &InputChans)) goto Error;
    if (!_cmsReadUInt16Number(io, &OutputChans)) goto Error;

    if (InputChans == 0 || InputChans >= cmsMAXCHANNELS) goto Error;
    if (OutputChans == 0 || OutputChans >= MAX_STAGE_CHANNELS) goto Error;

    if (!_cmsReadUInt16Number(io, &EncodingType)) goto Error;
    if (!_cmsReadUInt16Number(io, &Reserved2)) goto Error;

    // Tolerate the field written as a uInt32Number. Reserved bytes shall be zero, so a
    // non zero low half with a zero high half can only be the uInt32 spelling.
    if (EncodingType == 0 && Reserved2 != 0) EncodingType = Reserved2;

    if (io ->Read(io, Dimensions8, sizeof(cmsUInt8Number), 16) != 16)
        goto Error;

    // Copy MAX_INPUT_DIMENSIONS at most. Expand to cmsUInt32Number
    nMaxGrids = InputChans > MAX_INPUT_DIMENSIONS ? (cmsUInt32Number) MAX_INPUT_DIMENSIONS : InputChans;

    for (i = 0; i < nMaxGrids; i++) {

        if (Dimensions8[i] == 1) goto Error;   // Impossible value, 0 for no CLUT and then 2 at least
        GridPoints[i] = (cmsUInt32Number) Dimensions8[i];
    }

    // How many bytes each sample takes, per ICC.2:2023 Table 8
    switch (EncodingType) {

    case 0: BytesPerSample = 4; break;       // float32Number
#ifndef CMS_NO_HALF_SUPPORT
    case 1: BytesPerSample = 2; break;       // float16Number
#endif
    case 2: BytesPerSample = 2; break;       // uInt16Number
    case 3: BytesPerSample = 1; break;       // uInt8Number

    default:
        cmsSignalError(self ->ContextID, cmsERROR_UNKNOWN_EXTENSION,
            "Unsupported extendedCLUT value encoding type '%d'", EncodingType);
        goto Error;
    }

    // Work out how many samples the declared grid implies, and refuse the element if the
    // data it claims to hold does not fit in the bytes the position table gave it. Without
    // this a small element can declare a huge grid, and the parser would only notice once
    // the reads below ran off the end of the profile.
    nEntries = OutputChans;

    for (i = 0; i < nMaxGrids; i++) {

        if (GridPoints[i] == 0 || nEntries > UINT_MAX / GridPoints[i]) goto Error;
        nEntries *= GridPoints[i];
    }

    if (SizeOfTag < EXTCLUT_HEADER_BYTES) goto Error;
    if (nEntries > (SizeOfTag - EXTCLUT_HEADER_BYTES) / BytesPerSample) goto Error;

    // Allocate the true CLUT. This is the library's own allocator: the grid maths, the
    // interpolation parameters and the evaluator all come from it, and only the sample
    // decoding below belongs to this plug-in.
    mpe = cmsStageAllocCLutFloatGranular(self ->ContextID, GridPoints, InputChans, OutputChans, NULL);
    if (mpe == NULL) goto Error;

    // Remember that this element came in as an extendedCLUT, so a writer can find this
    // handler again. Implements stays at cmsSigCLutElemType: it *is* a CLUT, it is merely
    // encoded differently, and the pipeline optimizers key off Implements.
    mpe ->Type = ICCMAX_SigExtCLutElemType;

    clut = (_cmsStageCLutData*) mpe ->Data;

    // Read the data in whichever encoding the element declares
    switch (EncodingType) {

    case 0: // float32Number
        for (i = 0; i < clut ->nEntries; i++) {

            if (!_cmsReadFloat32Number(io, &clut ->Tab.TFloat[i])) goto Error;
        }
        break;

#ifndef CMS_NO_HALF_SUPPORT
    case 1: // float16Number
        for (i = 0; i < clut ->nEntries; i++) {

            if (!_cmsReadFloat16Number(io, &clut ->Tab.TFloat[i])) goto Error;
        }
        break;
#endif

    case 2: // uInt16Number, encoding 0.0 to 1.0
        for (i = 0; i < clut ->nEntries; i++) {

            cmsUInt16Number v;

            if (!_cmsReadUInt16Number(io, &v)) goto Error;
            clut ->Tab.TFloat[i] = (cmsFloat32Number) v / 65535.0f;
        }
        break;

    case 3: // uInt8Number, encoding 0.0 to 1.0
        for (i = 0; i < clut ->nEntries; i++) {

            cmsUInt8Number v;

            if (!_cmsReadUInt8Number(io, &v)) goto Error;
            clut ->Tab.TFloat[i] = (cmsFloat32Number) v / 255.0f;
        }
        break;

    default:
        goto Error;
    }

    *nItems = 1;
    return mpe;

Error:
    *nItems = 0;
    if (mpe != NULL) cmsStageFree(mpe);
    return NULL;
}

// Write an extended CLUT. Always emitted as float32Number, which is lossless for whatever
// encoding it was read from.
static
cmsBool Type_MPEextclut_Write(struct _cms_typehandler_struct* self, cmsIOHANDLER* io,
                              void* Ptr, cmsUInt32Number nItems)
{
    cmsUInt8Number Dimensions8[16];   // 16 because the spec says 16, not cmsMAXCHANNELS
    cmsUInt32Number i;
    cmsStage* mpe = (cmsStage*) Ptr;
    _cmsStageCLutData* clut = (_cmsStageCLutData*) mpe ->Data;

    if (mpe ->InputChannels > MAX_INPUT_DIMENSIONS) return FALSE;

    // Only floats are supported in MPE
    if (clut ->HasFloatValues == FALSE) return FALSE;

    if (!_cmsWriteUInt16Number(io, (cmsUInt16Number) mpe ->InputChannels)) return FALSE;
    if (!_cmsWriteUInt16Number(io, (cmsUInt16Number) mpe ->OutputChannels)) return FALSE;

    // valueEncodingType 0, float32Number, as a uInt16Number plus 2 reserved bytes
    if (!_cmsWriteUInt16Number(io, 0)) return FALSE;
    if (!_cmsWriteUInt16Number(io, 0)) return FALSE;

    memset(Dimensions8, 0, sizeof(Dimensions8));

    for (i = 0; i < mpe ->InputChannels; i++)
        Dimensions8[i] = (cmsUInt8Number) clut ->Params ->nSamples[i];

    if (!io ->Write(io, 16, Dimensions8)) return FALSE;

    for (i = 0; i < clut ->nEntries; i++) {

        if (!_cmsWriteFloat32Number(io, clut ->Tab.TFloat[i])) return FALSE;
    }

    return TRUE;

    cmsUNUSED_PARAMETER(nItems);
    cmsUNUSED_PARAMETER(self);
}

// The library's own MPE handlers set these to thin wrappers over cmsStageDup and
// cmsStageFree. Those wrappers are file-static in cmstypes.c, so the same two lines are
// here; both call straight into public API and neither parses anything.
static
void* MPEextclut_Dup(struct _cms_typehandler_struct* self, const void* Ptr, cmsUInt32Number n)
{
    return (void*) cmsStageDup((cmsStage*) Ptr);

    cmsUNUSED_PARAMETER(n);
    cmsUNUSED_PARAMETER(self);
}

static
void MPEextclut_Free(struct _cms_typehandler_struct* self, void* Ptr)
{
    cmsStageFree((cmsStage*) Ptr);

    cmsUNUSED_PARAMETER(self);
}


// ********************************************************************************
// embeddedProfileType ('ICCp'), carried by the 'ICC5' tag
// ********************************************************************************
//
// Per the ICC technical note "Embedding an ICC.2 (iccMAX) profile in an ICC.1 profile",
// Table 1:
//
//     0..3   'ICCp' type signature   ] this is exactly the standard tag base, which the
//     4..7   reserved, shall be 0    ] library's _cmsReadTypeBase already consumed
//     8..    the ICC.2 profile, in its entirety
//
// so SizeOfTag is the length of the embedded profile and no offset arithmetic is needed.
//
// Returned as a cmsICCData whose data can be handed straight to cmsOpenProfileFromMem.
// The length has to live inside the object: DupPtr is called with the tag descriptor's
// ElemCount, a fixed per-tag constant of 1, and not with a byte count, so a handler that
// trusted its n argument would duplicate a single byte of the profile.

static
void* Type_EmbeddedProfile_Read(struct _cms_typehandler_struct* self, cmsIOHANDLER* io,
                                cmsUInt32Number* nItems, cmsUInt32Number SizeOfTag)
{
    cmsICCData* Embedded;

    *nItems = 0;

    // A profile is at least a 128 byte header plus a 4 byte tag count
    if (SizeOfTag < 132) return NULL;
    if (SizeOfTag > INT_MAX) return NULL;

    Embedded = (cmsICCData*) _cmsMalloc(self ->ContextID, sizeof(cmsICCData) + SizeOfTag - 1);
    if (Embedded == NULL) return NULL;

    Embedded ->len  = SizeOfTag;
    Embedded ->flag = 0;

    if (io ->Read(io, Embedded ->data, sizeof(cmsUInt8Number), SizeOfTag) != SizeOfTag) {

        _cmsFree(self ->ContextID, Embedded);
        return NULL;
    }

    *nItems = 1;
    return (void*) Embedded;
}

static
cmsBool Type_EmbeddedProfile_Write(struct _cms_typehandler_struct* self, cmsIOHANDLER* io,
                                   void* Ptr, cmsUInt32Number nItems)
{
    cmsICCData* Embedded = (cmsICCData*) Ptr;

    // The bytes are passed through verbatim: nothing here authors ICC.2 content
    return io ->Write(io, Embedded ->len, Embedded ->data);

    cmsUNUSED_PARAMETER(nItems);
    cmsUNUSED_PARAMETER(self);
}

static
void* Type_EmbeddedProfile_Dup(struct _cms_typehandler_struct* self, const void* Ptr, cmsUInt32Number n)
{
    const cmsICCData* Embedded = (const cmsICCData*) Ptr;

    // The length comes from the object, not from n. See the note above.
    return _cmsDupMem(self ->ContextID, Ptr, sizeof(cmsICCData) + Embedded ->len - 1);

    cmsUNUSED_PARAMETER(n);
}

static
void Type_EmbeddedProfile_Free(struct _cms_typehandler_struct* self, void* Ptr)
{
    _cmsFree(self ->ContextID, Ptr);
}


// ********************************************************************************
// ICC.2 formulaCurveSegment function types 0003h to 0007h
// ********************************************************************************
//
// ICC.1 defines only function types 0, 1 and 2, which Little-CMS already has as parametric
// types 6, 7 and 8. ICC.2:2023 Table 111 adds five more; under the "lcms type = ICC type
// + 6" convention that core already establishes, those are lcms types 9 to 13.
//
// Registering them here is enough for both evaluation and reading, because the segmented
// curve reader asks the curve collections for the parameter count rather than keeping its
// own table. No part of the curve reader knows what iccMAX is.
//
// Parameter order follows Table 111. Note that omega precedes gamma in types 12 and 13;
// every other type has gamma as Params[0].
//
// Degenerate cases return a finite value rather than a NaN or an infinity, matching how
// the built-in types 6 and 7 behave: a NaN reaching a CLUT index is far worse than a
// clamped number. The analytic inverses (-9 to -13) are deliberately not implemented --
// curve reversal in lcms is numerical, so no profile path needs them.

// Exponentiation guarded against a non-positive base, which pow() would turn into a NaN
// for a fractional exponent. Mirrors clipPow in the iccMAX reference implementation, and
// is used for every power below including each X^gamma: a segmented curve's outer segments
// extend to +/- infinity, so a negative X is reachable.
static
cmsFloat64Number clipPow(cmsFloat64Number v, cmsFloat64Number g)
{
    if (v <= 0) return 0.0;

    return pow(v, g);
}

static
cmsFloat64Number IccMaxEvalCurve(cmsInt32Number Type, const cmsFloat64Number Params[], cmsFloat64Number R)
{
    switch (Type) {

    // Y = a * (b * X + c)^g + d          : g a b c d
    case 9:
        return Params[1] * clipPow(Params[2] * R + Params[3], Params[0]) + Params[4];

    // Y = a * ln(d * X^g - b) + c        : g a b c d
    case 10:
        {
            cmsFloat64Number e = Params[4] * clipPow(R, Params[0]) - Params[2];

            if (e <= 0) return Params[3];

            return Params[1] * log(e) + Params[3];
        }

    // Y = e * exp((d * X^g - c) / a) + b : g a b c d e
    case 11:
        if (fabs(Params[1]) < MATRIX_DET_TOLERANCE) return Params[2];

        return Params[5] * exp((Params[4] * clipPow(R, Params[0]) - Params[3]) / Params[1]) + Params[2];

    // Y = d * (max(e * X^g - a, 0) / (b - c * X^g))^w    : w g a b c d e
    case 12:
        {
            cmsFloat64Number u = clipPow(R, Params[1]);
            cmsFloat64Number den = Params[3] - Params[4] * u;
            cmsFloat64Number num = Params[6] * u - Params[2];

            if (num < 0) num = 0;

            if (fabs(den) < MATRIX_DET_TOLERANCE) return 0;

            return Params[5] * clipPow(num / den, Params[0]);
        }

    // Y = d * ((a + b * X^g) / (1 + c * X^g))^w          : w g a b c d
    case 13:
        {
            cmsFloat64Number u = clipPow(R, Params[1]);
            cmsFloat64Number den = 1.0 + Params[4] * u;

            if (fabs(den) < MATRIX_DET_TOLERANCE) return 0;

            return Params[5] * clipPow((Params[2] + Params[3] * u) / den, Params[0]);
        }

    default:
        // Includes the negative types, the analytic inverses, which are not implemented
        return 0;
    }
}


// ********************************************************************************
// The plug-in list
// ********************************************************************************
//
// Chained back to front so that cmsGetIccMaxPlugin can return a single head. Every entry
// is an addition: none of these four signatures is handled by the library.

static cmsPluginParametricCurves IccMaxCurvesPlugin = {

    { cmsPluginMagicNumber, 2060, cmsPluginParametricCurveSig, NULL },

    5,                                    // Five function types
    { 9, 10, 11, 12, 13 },                // ICC.2 Table 111 types 3 to 7, plus 6
    { 5,  5,  6,  7,  6 },                // Parameters each takes
    IccMaxEvalCurve
};

static cmsPluginTag IccMaxEmbeddedTagPlugin = {

    { cmsPluginMagicNumber, 2060, cmsPluginTagSig, (cmsPluginBase*) &IccMaxCurvesPlugin },

    ICCMAX_SigEmbeddedV5Tag,
    { 1, 1, { ICCMAX_SigEmbeddedProfileType }, NULL }
};

static cmsPluginTagType IccMaxEmbeddedTypePlugin = {

    { cmsPluginMagicNumber, 2060, cmsPluginTagTypeSig, (cmsPluginBase*) &IccMaxEmbeddedTagPlugin },

    { ICCMAX_SigEmbeddedProfileType,
      Type_EmbeddedProfile_Read, Type_EmbeddedProfile_Write,
      Type_EmbeddedProfile_Dup,  Type_EmbeddedProfile_Free, NULL, 0 }
};

static cmsPluginMultiProcessElement IccMaxExtClutPlugin = {

    { cmsPluginMagicNumber, 2060, cmsPluginMultiProcessElementSig,
      (cmsPluginBase*) &IccMaxEmbeddedTypePlugin },

    { (cmsTagTypeSignature) ICCMAX_SigExtCLutElemType,
      Type_MPEextclut_Read, Type_MPEextclut_Write,
      MPEextclut_Dup, MPEextclut_Free, NULL, 0 }
};

cmsPluginBase* CMSEXPORT cmsGetIccMaxPlugin(void)
{
    return (cmsPluginBase*) &IccMaxExtClutPlugin;
}
