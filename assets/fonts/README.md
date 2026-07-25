# Bundled UI fonts

These fonts back the editor/tool UI text rendering (mye_imgui `LoadEditorFonts`)
and cover Korean + Japanese + Simplified Chinese so the i18n UI renders without
missing-glyph boxes.

| File | Script | License | Source |
|------|--------|---------|--------|
| `HaFont.ttf` (haruna) | Latin + Korean | ⚠️ **provenance unverified** | vendored from an existing tool; **verify license before public redistribution** |
| `MPLUSRounded1c-Regular.ttf` | Japanese (kana + kanji), rounded | SIL Open Font License 1.1 | [Google Fonts / M+ FONTS](https://github.com/google/fonts/tree/main/ofl/mplusrounded1c) |
| `ZCOOLKuaiLe-Regular.ttf` | Simplified Chinese, rounded | SIL Open Font License 1.1 | [Google Fonts / ZCOOL](https://github.com/google/fonts/tree/main/ofl/zcoolkuaile) |

The two rounded CJK fonts are licensed under the SIL OFL 1.1 — free for
commercial use, redistribution allowed with the license kept intact. See the
upstream `OFL.txt` in each linked directory.

> `HaFont.ttf` is used for Korean per project instruction; its original license
> is not documented here. Before shipping/redistributing publicly, replace it
> with a font whose license is confirmed (e.g. Binggrae 빙그레체, which is free
> for commercial use) or verify HaFont's terms.
