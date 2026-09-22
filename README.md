# ReadGuard — Is Your Text Actually Readable?

**Unreal Engine 5.8 · Win64 · one runtime module · source included · no third-party code**

The engine scales your text. It never measures it.

There is no place in Unreal that will tell you *"this line is nine pixels tall at 720p"* — and that
is the line somebody on a handheld is squinting at while you look at your interface on a 1440p
monitor with the DPI curve quietly doing something else in between.

ReadGuard measures every text block your interface is drawing, right now, on screen, and answers
four questions about each one.

| # | Question | How it is answered |
|---|----------|--------------------|
| 1 | **How tall is it, really?** | The glyph height the font cache actually produces at the scale Slate is actually drawing at, in device pixels — then converted onto the smallest resolution you support (1280×720 by default) through your project's own DPI curve. Under the floor (14 px by default) it is a finding with the widget's name and the measured height. |
| 2 | **Does it fit its box?** | Desired size against allotted size. Clipped, wrapped where it must not, or spilling over its border. This is the bug that arrives with translation: the box was built for English, German is longer, nobody retests. |
| 3 | **Does it survive 200 % text scale?** | ReadGuard really sets the application scale, lets Slate lay the interface out again, and takes the same two measurements a second time. What breaks *only* at 200 % is reported separately. |
| 4 | **Is the contrast enough?** | Relative luminance and the standard ratio, against **4.5:1** for normal text and **3:1** for large — measured against the **rendered frame** under the text box, not against a guess at the parent widget's colour. |

## The two honest bits

**Contrast over a moving background is a hint, not an error.** Nobody knows what is behind a piece
of text without the finished image. ReadGuard reads the captured frame under the text box, discards
the pixels that are the glyphs themselves, and reports the dominant background and the spread. Where
the background genuinely varies — text over a moving scene, over video, over particles — the line
says **`background varies`**, carries the worst value it measured, and is an advisory that can never
fail a gate. A contrast number that pretends the background is constant would be worse than no
number at all.

**Green means checked, not clean.** ReadGuard measures what is on screen. A menu nobody opened was
not checked. Every report therefore states how many text blocks were visited and names the screens
it saw, on the panel and in the JSON. Green must never be able to mean "did not look".

## Quick start

1. Enable the plugin. `Project Settings → Plugins → ReadGuard` has the thresholds.
2. Set your game mode's **HUD Class** to `ReadGuardHUD` — or leave your own HUD alone and turn on
   *Auto Draw On Any HUD*.
3. Play. A scan runs automatically 1.5 s after begin play, and the panel appears.
4. `ReadGuard.ScanAt200` for the accessibility pass.

```
ReadGuard   FAIL   checked at 100% and 200%
text blocks 47 visited | errors 3  warnings 5 | smallest 9.2 px at 1280x720 (limit 14) | worst contrast 2.1:1 (limit 4.5) | scan 8.7 ms
screens checked: WBP_ReadGuardDemoScreen (31), WBP_PlayerHUD (16)
error   too small   WBP_ReadGuardDemoScreen.TinyHint    9.2 px (limit 14)   "Press E to interact"
error   low contrast WBP_ReadGuardDemoScreen.GreyNotice  2.1:1 (limit 4.5)  "Autosaving..."
warning breaks at large scale WBP_ReadGuardDemoScreen.ButtonLabel  at 200% scale  "Einstellungen"
```

## Console commands

| Command | What it does |
|---------|--------------|
| `ReadGuard.Scan` | Measure every visible text block now. |
| `ReadGuard.ScanAt200` | Measure at 100 % and then at the large text scale, and report what broke in between. |
| `ReadGuard.Show` / `.Hide` | The on-screen report. |
| `ReadGuard.Dump` | The whole last report to the log, with the sentence for every finding. |
| `ReadGuard.Report [path]` | Write the last report as JSON. |
| `ReadGuard.Target <w> <h>` | Measure against another resolution for this session. |
| `ReadGuard.Exempt [0\|1]` | Use the exemption list, or do not — to see what it is hiding. |
| `ReadGuard.Gate [path] [-noexit]` | Measure at both scales, write `Saved/ReadGuard/report.json`, exit **0** clean / **1** warnings / **2** errors. |

Those three exit codes are the same convention as LocaleGuard, AssetWarden, WidgetLedger, LoadLens,
HeapCensus and BindGuard.

## Blueprint

`UReadGuardStatics` exposes the whole thing, and the arithmetic half of it is static and world-free
so it can be called from your own tooling and is covered by automation tests (`ReadGuard.*`):
`RelativeLuminance`, `ContrastRatio`, `EffectivePixelHeight`, `IsLargeText`, `Judge`.

## Where this sits next to the neighbours

- **WidgetLedger** counts what your interface *costs*. ReadGuard asks whether it can be *read*.
  Same assets, opposite question.
- **CaptionCue** *produces* subtitles and offers the options a console check asks for — text size,
  background box, line width, safe area. ReadGuard produces nothing and *measures the whole
  surface*, CaptionCue's output included. One gives you the dials; the other tells you whether the
  setting is enough.

## No legal promise

ReadGuard measures against the usual accessibility thresholds. It does not make anyone compliant
with anything, and it never claims to.

---

Full documentation: **<https://wiki.teufel-engineering.com/en/readguard/documentation>**
Support: <mailto:teufelsilvan@gmail.com>
