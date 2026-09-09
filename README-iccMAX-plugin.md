# iccMAX hybrid printer reflectance — a Little-CMS plug-in

This branch reads and authors the spectral reflectance transform of an **ICC.2 (iccMAX) hybrid
printer profile with reflectance**, through a Little-CMS *plug-in* rather than by adding iccMAX to
the library.

It is a fork of [Little-CMS](https://github.com/mm2/Little-CMS), tracking upstream `master`.

## What it is for

It implements **Scenario 3** of the ICC Interoperability Conformance Specification
*hybridPrinterWithReflectance* (HPWR) — clause 5.2.3.4, *"Getting Spectral PCS data directly from a
hybrid printer profile"*. The ICS package exercises that scenario with
`config/hpwr-S3-SpectralPcsAccess.json`, and describes it as the simplest of the v5 scenarios,
because it uses only the forward transform of the embedded sub-profile and needs no iccMAX PCS
processing at all. The ICS characterises it as implementable "with limited extension of MPE
processing defined in ISO 15076-1", which is a fair description of the approach taken here.

The ICS and its reference package are published by the ICC at
<https://github.com/InternationalColorConsortium/ics>, under `packages/HybridPrinterWithReflectance`.

Scenarios 1 and 2 of the ICS are reachable by systems that support only ICC.1 profiles. Scenarios 4
to 6 require PCC overrides and real spectral PCS operation, which this plug-in deliberately does not
attempt. Scenario 3 is the smallest step that needs iccMAX at all, and it is the whole of the scope
here.

## How it works

Everything iccMAX-specific lives in `testbed/iccmax_plugin.c`, registered through plug-in hooks that
Little-CMS already provides. The plug-in only *adds* handlers; it does not override or re-implement
anything the library already does, so it inherits every fix made upstream to the shared readers.

It registers:

| Signature | What |
|---|---|
| `xclt` | `extendedCLUTElement`, a multi process element type |
| `ICCp` / `ICC5` | the embedded ICC.2 profile tag type and its tag |
| `fl16` / `fl32` | `float16ArrayType` and `float32ArrayType` |
| `swpt` | `spectralWhitePointTag`, in all three encodings ICC.2 permits |
| `svcn` | `spectralViewingConditionsType` and its tag |
| curve types 9–13 | ICC.2 `formulaCurveSegment` function types 3–7, which ICC.1 does not define |
| header hook | the spectral PCS fields at header bytes 100–109 |

Nothing here re-implements `cvst`, `clut`, `matf`, `mpet`, `curf`, `parf`, `samf` or `sngf`.

The public surface is in `testbed/iccmax_plugin.h`, prefixed `IccMaxRef` — it is a *reference*
implementation of one ICS, not general iccMAX support.

## Status

Working today: reading a hybrid printer profile's spectra, and authoring one from nothing — spectral
PCS header fields, `swpt`, `svcn` and a `DToB3` pipeline — verified against reference values produced
by the ICC reference implementation rather than by this code's own writer.

**This branch still carries changes to the library**, and is not yet a self-contained plug-in:

| Change | Status |
|---|---|
| Multi process element channel ceiling, plus an integer overflow guard in the CLUT allocators | still in the library |
| Plug-in-aware segmented-curve serialization | expected to be replaced by an upstream curve extension point |
| `singleSampledCurve` (`sngf`) reading | expected to be replaced by the same |
| Profile header extension | **done upstream** — uses `cmsPluginHeader` |
| Profile version gate | **done upstream** — uses the header plug-in's `ICCVersion` |

The remaining two curve items are the subject of an upstream discussion about a generalized curve
extension API. Once that lands, the only library change left should be the channel ceiling.

## Building and running the tests

The plug-in is compiled into the test suite, so the project's own build systems cover it — autotools,
meson, and the VC2019/2022/2026 `testbed` projects.

```sh
./configure && make && make check
```

The suite must be run with `testbed/` as the working directory, since several checks load
`testbed/HybridPrinterCMYK_small.icc`. That fixture is a subsampled derivative of the ICS package's
example profile, reduced to a committable size; every value in it is an original node value rather
than a resampled one, which makes it structurally faithful but far too coarse to be colorimetrically
useful. `testbed/subsample_clut.py` regenerates it.

## Licence

Same as Little-CMS. See `LICENSE`.
