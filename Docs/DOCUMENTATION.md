# ReadGuard — Documentation

**Unreal Engine 5.8 · Win64 · one runtime module · full C++ source included · no third-party code**

Online copy of this page: **<https://wiki.teufel-engineering.com/en/readguard/documentation>**

---

## Contents

1. [What ReadGuard does](#1-what-readguard-does)
2. [Supported engine and platforms](#2-supported-engine-and-platforms)
3. [Installation](#3-installation)
4. [Quick start — five minutes](#4-quick-start--five-minutes)
5. [The demo map](#5-the-demo-map)
6. [How each measurement works](#6-how-each-measurement-works)
7. [Why contrast over a moving background is a hint and not an error](#7-why-contrast-over-a-moving-background-is-a-hint-and-not-an-error)
8. [Green means checked, not clean — what the visit counter means](#8-green-means-checked-not-clean--what-the-visit-counter-means)
9. [Settings reference](#9-settings-reference)
10. [Console commands](#10-console-commands)
11. [The gate, and the JSON report](#11-the-gate-and-the-json-report)
12. [Class and API overview](#12-class-and-api-overview)
13. [Code examples](#13-code-examples)
14. [Automation tests](#14-automation-tests)
15. [What ReadGuard does not do](#15-what-readguard-does-not-do)
16. [Troubleshooting](#16-troubleshooting)
17. [Support](#17-support)

---

## 1. What ReadGuard does

ReadGuard measures every text block your interface is drawing and judges it against four thresholds.
It is a measuring instrument, not a monitor: it runs when you ask it to, and it never runs every
frame unless you deliberately set a timer.

The four measurements, per text block:

1. **Effective pixel height** — the glyph height the font cache actually produces at the layout scale
   Slate is actually drawing at, converted onto the smallest resolution you support.
2. **Fit** — desired size against allotted size: clipped text, or text drawn outside its box.
3. **The same two again at 200 % text scale** — really re-laid out, not predicted.
4. **Contrast** against the rendered frame under the text box.

The engine already scales your text through the DPI curve. Nothing in the engine ever *measures* the
result, and that measurement is the entire product.

---

## 2. Supported engine and platforms

| | |
|---|---|
| **Engine** | Unreal Engine **5.8** (`"EngineVersion": "5.8.0"` in the `.uplugin`) |
| **Platform** | **Win64** — the single runtime module carries `"PlatformAllowList": ["Win64"]` |
| **Modules** | One: `ReadGuard`, type `Runtime`, `LoadingPhase` `PreDefault` |
| **Configurations** | Verified building for **UnrealEditor Development**, **UnrealGame Development** and **UnrealGame Shipping** |
| **Where it runs** | Editor PIE, Standalone, and the cooked packaged build — the same code in all three |
| **Project types** | C++ and Blueprint-only projects both. Nothing in ReadGuard needs a C++ game module. |
| **Source** | Full C++ source ships with the plugin |
| **Third-party code** | None |
| **Engine module dependencies** | `Core`, `CoreUObject`, `Engine`, `UMG`, `Slate`, `SlateCore`, `DeveloperSettings` (public); `RenderCore`, `RHI`, `Json`, `JsonUtilities` (private) |

**There is deliberately no editor module and no `UnrealEd` dependency.** Everything ReadGuard claims
is a measurement of what is on screen at the moment somebody asks: the geometry Slate actually
allotted, the glyph height the font cache actually produced, the pixels the renderer actually drew.
None of that exists in the editor with no game standing, and all of it has to keep existing in a
cooked Shipping build — which is the build that gets submitted, and the build where a nine-pixel line
is still nine pixels.

Porting note for other platforms: nothing in the code is Windows-specific. The allow-list is Win64
because Win64 is what is tested and what the store listing promises, not because of a technical
limit.

---

## 3. Installation

1. Close the editor.
2. Copy the plugin folder to `YourProject/Plugins/ReadGuard`.
3. Reopen the project. If you are asked to rebuild missing modules, say yes.
4. `Edit → Plugins → Engine Tools → ReadGuard` — make sure it is enabled, and restart if the editor
   asks.
5. The thresholds now live under `Project Settings → Plugins → ReadGuard`.

> **If `/ReadGuard/` does not appear in the Content Browser**, the plugin has not been compiled into
> your project yet. A plugin's content root is only mounted once its module is built. Build the
> project's editor target once and reopen; the demo map appears then.

---

## 4. Quick start — five minutes

1. **Enable the plugin** (section 3). The defaults are 1280×720, 14 px, 4.5:1 / 3:1.

2. **Get the panel on screen.** Either set your Game Mode's **HUD Class** to `ReadGuardHUD`, or —
   if you already have your own HUD class and do not want to reparent it — leave it alone and switch
   on **Report → Auto Draw On Any HUD** in Project Settings instead. The two paths know about each
   other and cannot draw the panel twice.

3. **Press Play.** A scan runs automatically after **Auto Scan Delay Seconds** (1.5 s by default) and
   the panel appears in the top-left:

   ```
   ReadGuard  FAIL
   text blocks 18 visited | errors 5  warnings 0  (1 excluded by settings) | smallest 8.7 px at 1280x720 (limit 14) | worst contrast 2.1:1 (limit 4.5) | scan 0.3 ms
   screens checked: WBP_ReadGuardDemoControls (9), WBP_ReadGuardDemoScreen (9)
   error   too small      WBP_ReadGuardDemoScreen.TinyHint        8.7 px (limit 14)   "Hold [E] to interrupt the uplink sequenc..."
   error   too small      WBP_ReadGuardDemoScreen.AmmoCounter    10.1 px (limit 14)   "AMMO  24 / 120"
   error   clipped        WBP_ReadGuardDemoScreen.ObjectiveLabel  322 px past its box "Reach the extraction point before the st..."
   error   clipped        WBP_ReadGuardDemoScreen.ButtonCaption   256 px past its box "WEITERE INFORMATIONEN ANZEIGEN"
   error   low contrast   WBP_ReadGuardDemoScreen.LowContrastNote 2.1:1 (limit 3.0)   "Optional: recover the survey drone"
   info    too small      WBP_ReadGuardDemoScreen.VersionStamp    8.7 px (limit 14)   [excluded by settings]  "build 1.0.0-demo  rg"
   ```

   *(That is the demo map's bad screen, verbatim.)*

4. **Run the accessibility pass:** open the console and type `ReadGuard.ScanAt200`. ReadGuard measures
   at 100 %, sets the application scale to 200 %, lets Slate lay the whole interface out again, and
   measures a second time. What breaks *only* at the larger scale gets its own line:

   ```
   warning breaks at large scale  WBP_ReadGuardDemoScreen.SupplyLine  at 200% scale  "Supply drop arrives in 04:12 - hold the ..."
   ```

5. **Put it on the build server:** `ReadGuard.Gate` — see [section 11](#11-the-gate-and-the-json-report).

The 1.5 s delay in step 3 is not laziness. Widgets are created in `BeginPlay` and have no geometry
until Slate has ticked and laid them out; a scan in the same frame measures an interface that does
not exist yet.

---

## 5. The demo map

`Content/ReadGuard/Maps/L_ReadGuardDemo` is a working example you can open and play.

It contains two versions of the same screen. `WBP_ReadGuardDemoScreen` is deliberately badly built:
an 8 pt hint and a 10 pt ammo counter, an objective label a box cuts off 322 px short, a German
button caption that spills 256 px past its box, near-white text on a mid-grey plate at 2.1:1, and a
supply line that fits at 100 % and does not at 200 %. `WBP_ReadGuardDemoScreen_Fixed` is the same
screen built correctly, and it reports **ok** at both scales — worst contrast 5.4:1, nothing above
Info.

`WBP_ReadGuardDemoControls` is the bar along the bottom. Nine buttons: switch between the two
screens, scan, scan at 100 % and 200 %, flip the target resolution between 720p and 1080p, turn the
exemption list off and on, and write the JSON report. `BP_ReadGuardDemoGameMode` sets
`BP_ReadGuardDemoHUD` — a Blueprint child of `AReadGuardHUD` — as the HUD class, so the report panel
you see is the plugin's own, drawn on the canvas.

**The switching is the demo.** The point is not that the bad screen produces a red report; it is
that the difference between the two is *measured*, with numbers you can read off the panel, and that
those numbers change when you change the target resolution: the same 8 pt hint reads **8.7 px**
against 1280×720 and **13.0 px** against 1920×1080, and the ammo counter stops being a finding at the
larger target while the hint does not.

Both screens carry a `VersionStamp` that is deliberately tiny, and the plugin's settings exempt it.
It is still measured and still printed, as *excluded by settings* — press **EXEMPTIONS OFF** and it
turns back into the error it always was. That is the demo of the exemption list: it cannot hide
anything, it can only take the teeth out of it.

> The exemption that makes that line appear is a project setting, not a plugin setting, so it does
> not travel in the plugin. To reproduce it, add `VersionStamp` to **Exemptions → Exempt Widgets**
> under `Project Settings → Plugins → ReadGuard`.

---

## 6. How each measurement works

### 6.1 Effective pixel height

The font size you typed says nothing about what reaches the screen. Between it and the player there
is the font face's own metrics, the DPI curve under `Project Settings → User Interface`, and every
layout scale in the widget hierarchy.

ReadGuard takes the widget's live geometry, reads the accumulated layout scale off it, and asks the
Slate font measure service for the maximum character height of that exact font at that exact scale.
That is a device-pixel number, and it is what the renderer is really producing.

It is then converted onto the target resolution:

```
heightAtTarget = heightNow × dpiScale(targetResolution) ÷ dpiScale(currentViewport)
```

Both DPI scales come from your project's own curve, through the engine's own evaluation — so a
project with a hand-authored curve gets its own answer and not a guess at the default one. Both
numbers are printed in the report's `checksRun` line, e.g. *"converted onto 1280x720 through the
project DPI curve (0.920 here, 0.666 there)"*, so the arithmetic can be checked by hand.

The reported height is the font's **maximum character height** (ascender to descender), which is the
standard proxy for "how big does this look". It is not cap height and it is not the em size; the
threshold is calibrated for it.

### 6.2 Fit: clipped and overflow

Slate computes a *desired size* for every widget and then gives it an *allotted size*. When the
desired size is larger, the text does not fit. What happens next depends on clipping, and ReadGuard
walks up the Slate hierarchy to find out:

- Something between the text and the window clips → **Clipped**. Characters are lost. Error by
  default.
- Nothing clips → **Overflow**. Every character survives, drawn on top of whatever is next to it.
  Warning by default.

They are separate kinds because the repair is different, and a report that folded them together
would make its reader work that out again for every line.

**Overflow Tolerance** (1 px by default) exists because desired and allotted sizes disagree by
fractions through ordinary rounding all the time, and reporting every one of those is noise.

### 6.3 The 200 % pass

ReadGuard sets `UUserInterfaceSettings::ApplicationScale`, which is what the game viewport's DPI
scaler multiplies into every widget's layout scale. Slate lays the entire interface out again at the
new size and ReadGuard measures it there. It touches the game's interface only — never the editor
around it — and the scale is always put back, including if the level changes mid-pass or the
subsystem is torn down.

It waits **Settle Frames** (3 by default) after changing the scale before measuring. Measuring too
early reports the old layout at the new scale, which is a report full of failures that are entirely
the measurer's fault.

Only *new* problems are carried into the merged report, as the kind **breaks at large scale**. Text
that was already too small at 100 % is larger at 200 %, never smaller, and contrast does not change
with scale — so what the large pass really answers is whether the boxes still hold their text. That
is the line an accessibility review reads, and it gets its own kind so it can be found at a glance.

### 6.4 Contrast

The ratio itself is the standard one:

```
luminance = 0.2126·R + 0.7152·G + 0.0722·B      (on linear colour)
ratio     = (lighter + 0.05) ÷ (darker + 0.05)
```

Black is 0, white is 1, white on black is exactly 21. Normal text is judged against **4.5:1** and
large text against **3:1**; large means ≥ 24 px, or ≥ 18.66 px when the typeface is bold.

The text colour is the widget's specified colour, with its alpha and the Slate render opacity folded
in and then blended against the measured background — because a label at 40 % opacity is not the
colour that was authored, it is that colour mixed with whatever is underneath, and the mixture is
what a reader has to see. A text block whose colour comes from a style rather than being specified
directly is not contrast-checked, and the report says its contrast was not measured rather than
reporting it as fine.

The background is the hard part, and it has its own section.

---

## 7. Why contrast over a moving background is a hint and not an error

Nobody knows what is behind a piece of text without the finished image.

The tempting shortcut is to take the parent widget's brush colour and call that the background. It
is wrong the moment there is a 3D scene, a gradient, a video, a particle effect or another widget in
between — and it is wrong *silently*, which is worse, because it produces a confident number that
nobody has any reason to doubt.

So ReadGuard reads the pixels. It captures the rendered frame with the interface in it, restricted
to the game viewport, and samples the region under each text box on a grid.

The box contains the glyphs as well as what is behind them, so:

1. Pixels close to the known text colour are discarded — those are the text.
2. What is left goes into a luminance histogram of 24 bands.
3. The largest band is the **dominant background**, and its mean colour is what the reported contrast
   is measured against.
4. Every band covering at least 5 % of the remaining pixels contributes a contrast ratio. The worst
   of those is kept as `worstContrast`.
5. If two or more of those significant bands are further apart in luminance than **Background Varies
   Spread** (0.15 by default), the sample is flagged as varying.

**A varying background can never produce an error.** The finding is emitted at Info, it says
`background varies`, and the number it carries is the worst ratio measured rather than an average
over something that is not uniform. It can inform a person; it can never stop a build.

This is deliberate and it is the most important design decision in the plugin. A single contrast
number over a moving scene is a number pretending to be a fact. Given the choice between a confident
wrong number and an honest uncertain one, ReadGuard reports the honest one — and tells you the worst
case, which is the part you can actually act on. The fix for text over a scene is not "darken the
background" anyway; it is an outline, a drop shadow, or a backing box, and the sentence on the
finding says so.

Where too little background is left to measure — a box so full of glyphs that nothing survives the
filter, or a box mostly off screen — contrast is reported as **not measured** for that widget. Not
measured is not the same as fine.

### Cost

Capturing a frame costs a readback and a redraw. It is on by default because contrast is half the
reason to own this plugin, and it is a switch (**Contrast → Measure Contrast**) because a scan on a
timer in a shipped build should not be paying for it.

---

## 8. Green means checked, not clean — what the visit counter means

ReadGuard measures **what is on screen**. A menu nobody opened was not checked. A tooltip that never
appeared was not checked. A death screen that has not happened yet was not checked.

That is not a defect to apologise for; it is the price of measuring instead of guessing, and the only
thing that would be a defect is hiding it. So every report — on the panel, in the log and in the JSON
— carries:

- **`textBlocksVisited`** — how many text blocks were measured.
- **`textBlocksSkipped`** — how many were found but had no geometry or were not visible.
- **`screensChecked`** — the screens that were on display, **by name**, each with its own count.
- **`checksRun`** — the checks that ran, in words, including a line saying so when contrast was *not*
  measured.

When a report has nothing to say, the panel prints what it looked for instead of printing nothing:

```
ReadGuard  OK  checked at 100% and 200%
text blocks 18 visited | errors 0  warnings 0  (1 excluded by settings) | smallest 8.7 px at 1280x720 (limit 14) | worst contrast 5.4:1 (limit 4.5) | scan 0.7 ms
screens checked: WBP_ReadGuardDemoControls (9), WBP_ReadGuardDemoScreen_Fixed (9)
Nothing to report. What was checked:
  glyph height against 14 px, converted onto 1280x720 through the project DPI curve (0.920 here, 0.666 there)
  desired size against allotted size - clipped text and text drawn outside its box
  contrast against the rendered frame, 4.5:1 for normal text and 3.0:1 for large
  the same two size checks again at 200% text scale - 18 text block(s) seen, 0 thing(s) broke that were fine at 100%
```

A quiet checker is indistinguishable from a broken one, and that failure mode has cost more projects
more time than any false positive ever has.

A build script that wants to refuse a report which only saw four text blocks can read the number and
refuse it. That is the whole point of putting it first.

---

## 9. Settings reference

`Project Settings → Plugins → ReadGuard` — the class is `UReadGuardSettings`, a `UDeveloperSettings`
with `config = Game`, so the values land in `DefaultGame.ini` under
`[/Script/ReadGuard.ReadGuardSettings]`.

### Target

| Setting | Property | Default | Meaning |
|---|---|---|---|
| Target Resolution | `TargetResolution` | 1280×720 | Every measured height is converted onto this through the DPI curve. |
| Minimum Pixel Height | `MinimumPixelHeight` | 14 | The floor, in device pixels at the target resolution. |
| Normal Contrast Ratio | `NormalContrastRatio` | 4.5 | What normal text has to clear. |
| Large Contrast Ratio | `LargeContrastRatio` | 3.0 | What large text has to clear. |
| Large Text Pixels | `LargeTextPixels` | 24 | At and above this, text counts as large. |
| Large Text Bold Pixels | `LargeTextBoldPixels` | 18.66 | The same threshold for bold text. |
| Overflow Tolerance | `OverflowTolerance` | 1 | Pixels a text block may want beyond its box before that counts. |

Fourteen pixels is a defensible floor, not a law. A game played at arm's length on a handheld wants
more; a strategy game full of dense tables may argue for less. It is a setting because the right
answer depends on how far away the player is sitting, and no plugin knows that.

### Large Text

| Setting | Property | Default | Meaning |
|---|---|---|---|
| Large Scale Percent | `LargeScalePercent` | 200 | The scale the second pass runs at. |
| Settle Frames | `SettleFrames` | 3 | Frames to wait after changing the scale before measuring. |

### Contrast

| Setting | Property | Default | Meaning |
|---|---|---|---|
| Measure Contrast | `bMeasureContrast` | on | Capture the frame and measure contrast at all. |
| Background Varies Spread | `BackgroundVariesSpread` | 0.15 | Luminance spread above which a finding becomes an advisory. |
| Max Samples Per Box | `MaxSamplesPerBox` | 512 | Pixels sampled per text box, on a grid. |

### Severity

| Setting | Property | Default |
|---|---|---|
| Too Small | `TooSmallSeverity` | Error |
| Clipped | `ClippedSeverity` | Error |
| Overflow | `OverflowSeverity` | Warning |
| Low Contrast | `LowContrastSeverity` | Error |
| Breaks At Large Scale | `BreaksAtLargeScaleSeverity` | Warning |

A varying background is always Info regardless of what Low Contrast is set to.

### Exemptions

| Setting | Property | Default | Meaning |
|---|---|---|---|
| Use Exemptions | `bUseExemptions` | on | Master switch. `ReadGuard.Exempt 0` flips it for a session. |
| Exempt Widgets | `ExemptWidgets` | empty | Widget names allowed to break the rules. |

Every project has a debug overlay, a version stamp in the corner, a frame counter — text that is
deliberately tiny and deliberately low contrast. A tool that reports twenty errors on its first run
is a tool that gets switched off within the hour.

An exempt widget is **not skipped**. It is measured, its findings are produced, and then every one of
them is forced down to Info and counted as **excluded by settings** — a number the report always
shows. An exemption list that could hide its own effect would be a way to turn a project green by
editing a settings page. `ReadGuard.Exempt 0` is how you find out once a milestone what the list is
actually costing you.

Names are matched against the widget's own name (`VersionStamp`), not its path, because that is the
name the report prints and the name somebody will copy out of it.

### Report

| Setting | Property | Default | Meaning |
|---|---|---|---|
| Show Report By Default | `bShowReportByDefault` | on | Draw the panel from the first frame. |
| Scan On Begin Play | `bScanOnBeginPlay` | on | Scan once, shortly after the world starts. |
| Auto Scan Delay Seconds | `AutoScanDelaySeconds` | 1.5 | How long to wait for the interface to exist. |
| Auto Scan Interval Seconds | `AutoScanIntervalSeconds` | 0 (off) | Rescan on a timer. Off on purpose. |
| Max Report Rows | `MaxReportRows` | 14 | Findings listed before the panel says how many more. |
| Auto Draw On Any HUD | `bAutoDrawOnAnyHUD` | off | Draw the panel through `AHUD::OnHUDPostRender`. |
| Report Path | `ReportPath` | `Saved/ReadGuard/report.json` | Where the report and the gate write. |

---

## 10. Console commands

| Command | What it does |
|---|---|
| `ReadGuard.Scan` | Measure every visible text block now. |
| `ReadGuard.ScanAt200` | Measure at 100 %, then at the large text scale, and merge. |
| `ReadGuard.Show [0\|1]` / `ReadGuard.Hide` | The on-screen report. |
| `ReadGuard.Dump` | The whole last report to the log, with the sentence for every finding. |
| `ReadGuard.Report [path]` | Write the last report as JSON. |
| `ReadGuard.Target <w> <h>` | Measure against another resolution for this session, then rescan. |
| `ReadGuard.Exempt [0\|1]` | Use the exemption list, or do not, then rescan. |
| `ReadGuard.Gate [path] [-noexit]` | The gate. See below. |

Every one of these needs a running game. That is not a limitation to work around — there is no such
thing as measuring the text a game is drawing when the game is not drawing anything. With no game,
the commands say so and do nothing.

---

## 11. The gate, and the JSON report

```
ReadGuard.Gate
```

Measures at 100 % **and** at the large scale, writes `Saved/ReadGuard/report.json`, and ends the
process with:

| Exit code | Meaning |
|---|---|
| **0** | Clean — nothing above Info. |
| **1** | Warnings only. |
| **2** | At least one error. |

The same three codes as LocaleGuard, AssetWarden, WidgetLedger, LoadLens, HeapCensus and BindGuard,
meaning the same three things — a project that already owns one of these has nothing new to learn.

`-noexit` runs the whole gate and writes the report but leaves the process alive, which is what you
want when you are driving several screens in one session.

Two things a build script has to know:

- **It is not instantaneous.** The two passes need frames to settle and a frame to capture, so the
  exit happens some frames after the command rather than inside it.
- **It only judges what was on display.** Drive your game to the screen you want checked, then run
  the gate. The report writes down how many text blocks it saw and names them, so a script can refuse
  a report that was not allowed to check anything: a gate that passes on an empty screen has proved
  nothing.

If the report cannot be written, the gate exits **2**. A gate that did not run must never look like a
gate that passed. The same is true with no running game: the gate says so and exits **2**.

### Report format

```json
{
  "plugin": "ReadGuard",
  "reportVersion": 1,
  "verdict": "fail",
  "exitCode": 2,
  "hasRun": true,
  "textBlocksVisited": 18,
  "textBlocksSkipped": 0,
  "widgetsWalked": 39,
  "errors": 5,
  "warnings": 1,
  "infos": 0,
  "excludedBySettings": 0,
  "scalePercent": 100.0,
  "largeScaleTested": true,
  "largeScalePercent": 200.0,
  "largeScaleBreaks": 1,
  "targetWidth": 1920,
  "targetHeight": 1080,
  "targetDpiScale": 1.0,
  "viewportWidth": 1914,
  "viewportHeight": 994,
  "viewportDpiScale": 0.920211,
  "minimumPixelHeight": 14.0,
  "normalContrastThreshold": 4.5,
  "largeContrastThreshold": 3.0,
  "smallestPixelHeight": 13.0405,
  "smallestPixelHeightWidget": "WBP_ReadGuardDemoScreen.TinyHint",
  "contrastMeasured": true,
  "contrastMeasuredCount": 18,
  "backgroundVariesCount": 0,
  "worstContrast": 2.10715,
  "worstContrastWidget": "WBP_ReadGuardDemoScreen.LowContrastNote",
  "worstContrastBackgroundVaries": false,
  "scanMilliseconds": 0.693098,
  "checksRun": [ "glyph height against 14 px, converted onto 1920x1080 through the project DPI curve (0.920 here, 1.000 there)", "..." ],
  "screensChecked": [ { "name": "WBP_ReadGuardDemoScreen", "textBlocks": 9, "findings": 5 } ],
  "findings": [
    { "kind": "too small", "severity": "error", "path": "WBP_ReadGuardDemoScreen.TinyHint", "measured": 13.0405, "threshold": 14.0, "detail": "..." }
  ],
  "measurements": [
    { "path": "...", "pixelHeightNow": 13.8, "pixelHeightAtTarget": 13.04, "contrast": 2.11 }
  ]
}
```

*(Those values are a real report from the demo map, taken with the target set to 1920×1080 and the
exemption list switched off — which is why `excludedBySettings` is 0 and `VersionStamp` shows up as
an error rather than as an advisory.)*

Values that could not be measured are written as `null`, never as `0`, so a build script cannot
mistake *not measured* for *measured and fine*. The field names are a published interface and are
spelled out by hand rather than reflected off C++ member names — renaming a member will never rename
a field.

---

## 12. Class and API overview

| Class | Kind | What it is for |
|---|---|---|
| `UReadGuardSubsystem` | `UGameInstanceSubsystem`, `FTickableGameObject` | The measurer. Owns the passes, the frame capture, the merged report, the gate. |
| `UReadGuardStatics` | `UBlueprintFunctionLibrary` | The arithmetic and the Blueprint surface. World-free, testable. |
| `AReadGuardHUD` | `AHUD` | Draws the report on `UCanvas`. |
| `UReadGuardSettings` | `UDeveloperSettings` | The thresholds, under `Project Settings → Plugins → ReadGuard`. |
| `FReadGuardScanner` | plain C++ | The widget walk and the rules. Internal; the subsystem drives it. |

### Structs and enums (`ReadGuardTypes.h`, all `BlueprintType`)

| Type | What it carries |
|---|---|
| `FReadGuardFinding` | One thing that is wrong: `Kind`, `Severity`, `WidgetName`, `ScreenName`, `WidgetPath`, `TextPreview`, `MeasuredValue`, `Threshold`, `ScalePercent`, `bAtLargeScale`, `bBackgroundVaries`, `bExcluded`, `Detail`. |
| `FReadGuardMeasurement` | One text block as measured, before any rule judged it: `PixelHeightNow`, `PixelHeightAtTarget`, `FontSize`, `LayoutScale`, `bBold`, `OverflowX/Y`, `bClips`, `bAutoWrap`, `bContrastMeasured`, `Contrast`, `WorstContrast`, `TextColor`, `BackgroundColor`, `BackgroundSamples`. |
| `FReadGuardScreen` | One screen that was looked at: `Name`, `TextBlocks`, `Findings`. |
| `FReadGuardThresholds` | The thresholds a pass runs with, flattened out of the settings so a test can build one in four lines. |
| `FReadGuardReport` | The whole answer: findings, measurements, screens visited, counts, `SmallestPixelHeight`, `WorstContrast`, DPI scales, `bLargeScaleTested`, `LargeScaleBreakCount`, `ScanMilliseconds`, `Verdict`, `ChecksRun`, `bHasRun`. |
| `EReadFindingKind` | `TooSmall`, `Clipped`, `Overflow`, `LowContrast`, `BreaksAtLargeScale`. |
| `EReadSeverity` | `Info`, `Warning`, `Error`. |
| `EReadVerdict` | `Ok`, `Warn`, `Fail`. |

### `UReadGuardSubsystem`

| Function | Notes |
|---|---|
| `static UReadGuardSubsystem* Get(const UObject* WorldContextObject)` | The subsystem for that object's world, or null. |
| `bool Scan()` | Start a pass at the current scale. False if one is already running. |
| `bool ScanAtScale(float ScalePercent)` | Start a pass at that percentage of the normal text scale. |
| `bool ScanBothScales()` | 100 % then the large scale, merged. This is what the gate runs. |
| `FReadGuardReport MeasureNow()` | Synchronous. Current geometry, background from the last captured frame. |
| `bool IsScanning() const` | True while a pass is in flight. |
| `const FReadGuardReport& GetReport() const` | The last completed report. `bHasRun` is false until one completes. |
| `TArray<FReadGuardFinding> GetFindings() const` | |
| `EReadVerdict GetVerdict() const` | |
| `bool WriteReport(FString Path)` | Empty path means the one in Project Settings. |
| `FReadGuardScanComplete OnScanComplete` | `BlueprintAssignable`. Fires with the merged report when a run finishes. |
| `void SetReportVisible(bool)` / `bool IsReportVisible() const` | The on-screen panel. |
| `void DrawReport(UCanvas*, const FVector2D& Origin, float Width) const` | C++ only; called by the HUD. |
| `void SetTargetResolution(FIntPoint)` / `FIntPoint GetTargetResolution() const` | Live override, for this session. |
| `void SetExemptionsEnabled(bool)` / `bool AreExemptionsEnabled() const` | Live override, for this session. |
| `void ApplySettings()` | Re-read Project Settings, discarding any live override. |
| `void BeginGate(const FString& Path, bool bExitWhenDone)` | C++ only. What `ReadGuard.Gate` calls. |

### `UReadGuardStatics`

**The arithmetic** — pure functions of their arguments, no world, no subsystem, no widget. These are
what the automation tests cover, and they are public so your own tooling can call them with numbers
ReadGuard never saw.

| Function | |
|---|---|
| `float RelativeLuminance(const FLinearColor&)` | Black 0, white 1, by construction. Colour must be linear. |
| `float ContrastRatio(const FLinearColor& A, const FLinearColor& B)` | `(lighter + 0.05) / (darker + 0.05)`. Symmetric, bounded by 21. |
| `float ContrastRatioFromLuminance(float A, float B)` | The same when you already have the luminances. |
| `float ProjectDPIScaleForResolution(FIntPoint)` | Your project's own DPI curve, through the engine's evaluation. |
| `float ScalePixelHeight(float MeasuredPixels, float SourceDPIScale, float TargetDPIScale)` | |
| `float EffectivePixelHeight(float FontPixelHeight, float SourceDPIScale, float WidgetScale, FIntPoint TargetResolution)` | |
| `bool IsLargeText(float Pixels, bool bBold, float LargeTextPixels = 24.f, float LargeTextBoldPixels = 18.66f)` | |
| `float RequiredContrast(float Pixels, bool bBold, const FReadGuardThresholds&)` | |

**The verdict**

| Function | |
|---|---|
| `EReadVerdict Judge(const TArray<FReadGuardFinding>&)` | Fail on any Error, Warn on any Warning, else Ok. Info can never fail a gate. |
| `int32 VerdictExitCode(EReadVerdict)` | 0 / 1 / 2. |
| `FString VerdictName / KindName / SeverityName(...)` | |
| `int32 ApplyExemptions(TArray<FReadGuardFinding>& Findings, const TArray<FName>& ExemptWidgets)` | Forces matches down to Info and returns how many. |
| `void SortFindings(TArray<FReadGuardFinding>&)` | Errors first, then warnings, then info; worst measurement first within a severity. |

**Words and convenience**

| Function | |
|---|---|
| `FString Explain(const FReadGuardFinding&)` | One sentence saying what to *change*, not what is wrong. |
| `FString FormatFinding(const FReadGuardFinding&)` | The finding as one line. |
| `FString Headline(const FReadGuardReport&)` | The panel's first line. Visit count first, on purpose. |
| `FString FormatScreens(const FReadGuardReport&)` | Which screens were looked at, by name. |
| `FString TruncateText(const FString&, int32 MaxCharacters = 40)` | |
| `FReadGuardReport GetLastReport(const UObject* WorldContextObject)` | |
| `bool Scan(const UObject* WorldContextObject)` | |
| `bool ScanAtScale(const UObject* WorldContextObject, float ScalePercent)` | |

### `AReadGuardHUD`

| Member | |
|---|---|
| `void ToggleReport()` | Same switch as `ReadGuard.Show` / `.Hide`. |
| `bool IsReportVisible() const` | |
| `void ScanNow()` | |
| `void ScanAtLargeScale()` | 100 % then the large scale, merged. |
| `FVector2D PanelOrigin` | Default `(28, 90)`. |
| `float PanelWidth` | Default `980`. Wide, because a report whose important lines wrap is a report nobody reads twice. |

The panel is drawn on `UCanvas` and not in UMG, and that is a correctness argument rather than a
portability one: a report built out of UMG widgets would put its own text blocks into the interface
it is measuring. As a bonus it survives a cooked Shipping build with no widget assets loaded.

---

## 13. Code examples

### C++ — scan and react

```cpp
#include "ReadGuardSubsystem.h"
#include "ReadGuardStatics.h"

void AMyGameplayActor::CheckTheInterface()
{
    UReadGuardSubsystem* ReadGuard = UReadGuardSubsystem::Get(this);
    if (!ReadGuard)
    {
        return;
    }

    ReadGuard->OnScanComplete.AddDynamic(this, &AMyGameplayActor::HandleScan);

    // 100 % and then 200 %, merged - the same pass the gate runs.
    ReadGuard->ScanBothScales();
}

void AMyGameplayActor::HandleScan(const FReadGuardReport& Report)
{
    UE_LOG(LogTemp, Display, TEXT("%s"), *UReadGuardStatics::Headline(Report));

    // "Green means checked" - refuse a report that was not allowed to see anything.
    if (Report.TextBlocksVisited < 5)
    {
        UE_LOG(LogTemp, Warning, TEXT("ReadGuard saw %d text blocks. That is not a pass."),
            Report.TextBlocksVisited);
        return;
    }

    for (const FReadGuardFinding& Finding : Report.Findings)
    {
        if (Finding.Severity == EReadSeverity::Error && !Finding.bExcluded)
        {
            UE_LOG(LogTemp, Error, TEXT("%s  ->  %s"),
                *UReadGuardStatics::FormatFinding(Finding),
                *Finding.Detail);
        }
    }

    if (UReadGuardStatics::Judge(Report.Findings) == EReadVerdict::Fail)
    {
        ReadGuard_WriteArtifact();
    }
}
```

A scan is not instant — contrast needs a frame that has already been drawn, and the large pass needs
Slate to lay out again — so `Scan()` starts a pass and `OnScanComplete` says when it is over.
`MeasureNow()` is there for when you want an answer this instant and a background from a frame or two
ago is acceptable.

### C++ — the arithmetic on your own numbers

No world, no game, no widget. This runs in a commandlet, in a test, or in your own tooling:

```cpp
const float Luminance = UReadGuardStatics::RelativeLuminance(FLinearColor::White);   // 1.0
const float Black     = UReadGuardStatics::RelativeLuminance(FLinearColor::Black);   // 0.0
const float Ratio     = UReadGuardStatics::ContrastRatio(FLinearColor::White,
                                                         FLinearColor::Black);       // exactly 21.0

// 24 px measured where the DPI curve says 1.0, asked for a target whose curve says 0.6666.
const float AtTarget  = UReadGuardStatics::ScalePixelHeight(24.f, 1.0f, 0.6666f);    // ~16.0

// A 20 pt font inside a widget hierarchy at layout scale 1.5, measured at DPI 1.0,
// converted onto 1280x720 through the project's own curve.
const float Height    = UReadGuardStatics::EffectivePixelHeight(
                            /*FontPixelHeight=*/20.f,
                            /*SourceDPIScale=*/1.f,
                            /*WidgetScale=*/1.5f,
                            /*TargetResolution=*/FIntPoint(1280, 720));

const bool  bLarge    = UReadGuardStatics::IsLargeText(19.f, /*bBold=*/true);        // true
const bool  bNotLarge = UReadGuardStatics::IsLargeText(19.f, /*bBold=*/false);       // false
```

### C++ — judging findings you built yourself

```cpp
TArray<FReadGuardFinding> Findings;

FReadGuardFinding& F = Findings.AddDefaulted_GetRef();
F.Kind          = EReadFindingKind::TooSmall;
F.Severity      = EReadSeverity::Error;
F.WidgetName    = TEXT("VersionStamp");
F.MeasuredValue = 8.7f;
F.Threshold     = 14.0f;

// The exemption list defangs, it does not delete: after this, F.Severity is Info,
// F.bExcluded is true, and Excluded is 1.
const int32 Excluded = UReadGuardStatics::ApplyExemptions(Findings, { TEXT("VersionStamp") });

const EReadVerdict Verdict = UReadGuardStatics::Judge(Findings);   // Ok - nothing above Info left
const int32 ExitCode       = UReadGuardStatics::VerdictExitCode(Verdict);   // 0
```

### Blueprint

Everything above is on the `ReadGuard` category in the Blueprint palette.

- **`Scan`** / **`Scan At Scale`** (World Context) — start a pass from a button, a level Blueprint,
  or a cheat.
- **`Get Last Report`** (World Context) — the whole `FReadGuardReport` struct, breakable in the
  graph.
- **`Get Read Guard Subsystem`** → `Get Findings`, `Get Verdict`, `Write Report`,
  `Set Target Resolution`, `Set Exemptions Enabled`, `Set Report Visible`, `Apply Settings`.
- **`On Scan Complete`** — bind on the subsystem for the result.
- **ReadGuard|Math** — `Relative Luminance`, `Contrast Ratio`, `Effective Pixel Height`,
  `Is Large Text`, `Required Contrast`, `Project DPI Scale For Resolution`. All `BlueprintPure`.
- **ReadGuard|Report** — `Headline`, `Format Finding`, `Format Screens`, `Explain`, `Truncate Text`.

`Content/ReadGuard/UI/WBP_ReadGuardDemoControls` is a working example of every one of these wired to
a button.

### On a build machine

```bat
YourGame.exe -game -windowed -ResX=1280 -ResY=720 ^
             -ExecCmds="ReadGuard.Target 1280 720, ReadGuard.Gate"
echo exit code %ERRORLEVEL%
```

Drive the game to the screen you want checked first — the gate only judges what was on display.

---

## 14. Automation tests

Ten tests under `ReadGuard.*`, runnable from `Window → Test Automation` or with
`-ExecCmds="Automation RunTests ReadGuard"`:

| Test | What it proves |
|---|---|
| `ReadGuard.Math.RelativeLuminanceHitsBlackAndWhiteExactly` | 0 and 1, exactly. |
| `ReadGuard.Math.ContrastRatioOfWhiteOnBlackIsExactlyTwentyOne` | The bound is real. |
| `ReadGuard.Math.ContrastRatioIsSymmetric` | Argument order cannot change the answer. |
| `ReadGuard.Math.EffectivePixelHeightConvertsOntoTheTargetResolution` | A known DPI curve converts correctly. |
| `ReadGuard.Math.IsLargeTextSplitsAtTheThresholdAndBoldCountsEarlier` | Both thresholds, at the boundary. |
| `ReadGuard.Rules.AnExemptedFindingIsDefangedButStillCounted` | Exemptions cannot hide. |
| `ReadGuard.Rules.JudgeFailsOnlyWhenThereIsAnError` | Info can never fail a gate. |
| `ReadGuard.Rules.FindingsSortWorstFirst` | The panel means one thing in a screenshot. |
| `ReadGuard.Report.EveryKindOfFindingHasASentence` | No finding ships without a repair instruction. |
| `ReadGuard.Report.GreenNeverMeansNobodyLooked` | A report with nothing to say still says what it checked. |

They run in the editor and in a commandlet, and none of them needs a running game — which is the
whole reason the arithmetic is static and world-free.

---

## 15. What ReadGuard does not do

- It does not check text that is not on screen. See [section 8](#8-green-means-checked-not-clean--what-the-visit-counter-means).
- It measures `UTextBlock` and `URichTextBlock`. Editable text and combo boxes draw text too, but
  their content is the player's rather than the project's, and findings about whatever somebody last
  typed into a name field would be noise. Slate text built in C++ without a UMG widget in front of it
  is also not covered, because there would be no name to report.
- It does not measure per-run colours inside a rich text block. The default style is measured; a
  block whose decorators change colour mid-sentence will be caught by the varying-background rule
  instead.
- It does not check text rendered into a material, a render target or a 3D widget's texture.
- It does not translate anything, resize anything or fix anything. It measures and it reports.
- **It makes no legal promise.** ReadGuard measures against the usual accessibility thresholds. It
  does not make anyone compliant with anything, and it never claims to.

---

## 16. Troubleshooting

**"text blocks 0 visited"** — nothing was on screen when the scan ran, or the widgets had not been
laid out yet. Raise **Auto Scan Delay Seconds**, or run `ReadGuard.Scan` by hand once your interface
is up.

**"contrast not measured"** — either **Measure Contrast** is off, or no frame came back within the
capture timeout, or every text block's colour comes from a style rather than being specified. The
`checksRun` line says which.

**Every label reports an overflow of a pixel or two** — raise **Overflow Tolerance**.

**The 200 % pass reports things the 100 % pass did not, in screens that look fine** — that is what it
is for. Turn the scale up in a shipped game with an accessibility slider and you will see the same
thing.

**The panel is not drawn** — the game mode's HUD class is not `ReadGuardHUD` and **Auto Draw On Any
HUD** is off. Either will do.

**`/ReadGuard/` is missing from the Content Browser** — the plugin has not been compiled into the
project yet. See [section 3](#3-installation).

**The console commands say there is no running game** — they need one. There is no such thing as
measuring the text a game is drawing when the game is not drawing anything.

---

## 17. Support

Documentation: **<https://wiki.teufel-engineering.com/en/readguard/documentation>**
Support: <mailto:teufelsilvan@gmail.com>

---

*Copyright 2026 Silvan Teufel. All Rights Reserved.*
