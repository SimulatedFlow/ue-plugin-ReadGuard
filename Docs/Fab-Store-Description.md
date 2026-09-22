# ReadGuard — Is Your Text Actually Readable?

The engine scales your text. It never measures it.

There is no place in Unreal that will tell you *"this line is nine pixels tall at 720p"* — and that
is the line somebody on a handheld is squinting at while you build your interface on a 1440p monitor
with the DPI curve quietly doing something else in between.

ReadGuard measures every text block your interface is drawing, right now, on screen, and answers four
questions about each one.

## 1. How tall is it, really?

Not the font size you typed. The glyph height the font cache actually produces, at the layout scale
Slate is actually drawing at, in device pixels — then converted onto the smallest resolution you
support (1280×720 by default) through **your project's own DPI curve**, evaluated by the engine
rather than guessed at.

Under the floor (14 px by default) it is a finding with the widget's name, the screen it lives on,
the first words of the text so you can find it again, and the measured height:

```
error   too small   WBP_PlayerHUD.AmmoCounter   9.2 px (limit 14)   "24 / 120"
```

## 2. Does it fit its box?

Desired size against allotted size. Clipped, wrapped where it must not be, or spilling past its
border onto whatever is next to it. Those last two are separate findings because the repair is
different: clipped text loses characters, overflowing text keeps them and covers something else.

This is the bug that arrives with translation. The box was built for English, German is longer, and
nobody retests.

## 3. Does it survive 200 % text scale?

ReadGuard sets the application scale, lets Slate lay the entire interface out again, and takes the
same two measurements a second time. It does not predict what 200 % would do — a prediction is
exactly the thing this plugin exists to replace.

What breaks *only* at the large scale is reported as its own kind, because that is the line an
accessibility review reads, and because it is the one measurement no other tool in the engine
produces.

## 4. Is the contrast enough?

Relative luminance and the standard ratio, against **4.5:1** for normal text and **3:1** for large —
where large means 24 px, or 18.66 px when the typeface is bold.

Measured against the **rendered frame** under the text box, not against a guess at the parent
widget's colour.

## The honest part, and it is the reason to trust the rest

Nobody knows what is behind a piece of text without the finished image.

The tempting shortcut — take the parent widget's brush colour and call that the background — is wrong
the moment there is a 3D scene, a gradient, a video or another widget in between. And it is wrong
*silently*, which is worse: it produces a confident number nobody has any reason to doubt.

So ReadGuard reads the pixels. It captures the rendered frame, discards the pixels that are the
glyphs themselves (it knows what colour they are), puts what is left into a luminance histogram, and
reports the dominant band and the spread.

Where the background genuinely varies — text over a moving scene, over video, over particles — the
line says **`background varies`**, carries **the worst ratio it measured**, and is an advisory that
can never fail a gate.

A contrast number that pretends the background is constant would be worse than no number at all.

## Green means checked, not clean

ReadGuard measures what is on screen. A menu nobody opened was not checked.

So every report states **how many text blocks were visited** and **names the screens it saw** — on
the panel, in the log and in the JSON. When there is nothing to report, the panel prints what it
looked for instead of printing nothing. Green must never be able to mean "did not look", and a quiet
checker is indistinguishable from a broken one.

## One command on the build server

```
ReadGuard.Gate
```

Measures at 100 % **and** at 200 %, writes `Saved/ReadGuard/report.json`, and ends the process with
**0** clean, **1** warnings, **2** errors — the same three codes as LocaleGuard, AssetWarden,
WidgetLedger, LoadLens, HeapCensus and BindGuard, meaning the same three things. Values that could
not be measured are written as `null` and never as `0`, so a script cannot mistake *not measured* for
*measured and fine*.

## The exemption list, and why it cannot hide

Every project has a version stamp in the corner, a frame counter, a debug overlay — text that is
deliberately tiny and deliberately low contrast. A tool that reports twenty errors on its first run
is a tool that gets switched off within the hour.

Exempt widgets are named in Project Settings. An exempt widget is **not skipped**: it is measured, its
findings are produced, and then every one of them is forced down to Info and counted — the demo map's
report reads **`1 excluded by settings`** — a number the report always shows. `ReadGuard.Exempt 0` turns the list off for a
session so you can see once a milestone what it is really costing you.

## Drawn on the canvas, not in UMG

The report is drawn with `UCanvas` from an `AHUD`. That is not a portability argument, it is a
correctness one: a report built out of UMG widgets would put its own text blocks into the interface
it is measuring. As a bonus it survives a cooked Shipping build with no widget assets loaded at all.

Set `ReadGuardHUD` as your game mode's HUD class, or leave your own HUD alone and switch on *Auto
Draw On Any HUD*.

## Testable rules

The arithmetic — relative luminance, contrast ratio, effective pixel height, the large-text
threshold, the verdict — is static functions over plain values with no world, no subsystem and no
widget behind them. That is why it is covered by automation tests (`ReadGuard.*`, ten of them) and
why you can call it from your own tooling with numbers ReadGuard never saw.

## The demo map

`L_ReadGuardDemo` ships with two versions of the same screen: one deliberately badly built — an 8 pt
line, a button whose German caption overflows, light grey on white, and a box that only breaks at
200 % — and the same screen built correctly. A button switches between them.

The switching is the demo. Not that the bad screen turns the report red, but that the difference
between the two is **measured**, in numbers you can read off the panel, and that those numbers change
when you change the target resolution.

Here is what the bad screen actually reports — an editor session on one machine, targeting 1280×720:

`ReadGuard FAIL — text blocks 18 visited | errors 5  warnings 0 (1 excluded by settings) |
smallest 8.7 px at 1280x720 (limit 14) | worst contrast 2.1:1 (limit 4.5)`

with, underneath, every finding named: `TinyHint 8.7 px`, `AmmoCounter 10.1 px`,
`ObjectiveLabel 322 px past its box`, `LowContrastNote 2.1:1` — and the one that pays for the plugin:

`ButtonCaption 256 px past its box — "WEITERE INFORMATIONEN ANZEIGEN"`

A German caption in a box that was sized for English. That is the bug that appears the week you
localise, in a language nobody on the team reads, on a screen nobody reopens. Here it is a line in a
report instead of a review comment.

## Next to the neighbours

- **WidgetLedger** counts what your interface *costs*. ReadGuard asks whether it can be *read*. Same
  assets, opposite question — if you own one, the other answers what it cannot.
- **CaptionCue** *produces* subtitles and offers the options a console check asks for: text size,
  background box, line width, safe area. ReadGuard produces nothing and *measures the whole surface*,
  CaptionCue's output included. One gives you the dials; the other tells you whether the setting is
  enough.

## No legal promise

ReadGuard measures against the usual accessibility thresholds. It does not make anyone compliant with
anything, and it never claims to.

---

**Unreal Engine 5.8 · Win64 · one runtime module · full C++ source included · no third-party code**

Documentation: <https://wiki.teufel-engineering.com/en/ReadGuard/documentation>
Support: <mailto:teufelsilvan@gmail.com>

