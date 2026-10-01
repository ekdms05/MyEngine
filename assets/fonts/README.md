# UI font sources

`mye::imgui::LoadEditorFonts` uses NanumSquareRound Regular for editor Latin/Korean
text. CMake copies the font and license to the editor's `fonts/` directory.
The release package includes the font in `fonts/` and its full license in
`licenses/`. If the bundled font is missing, installed Malgun Gothic is used.
Installed Meiryo and Microsoft YaHei supplement Japanese and Simplified Chinese;
Windows system fonts are loaded in place and are not redistributed.

| File | Script | License | Source |
|------|--------|---------|--------|
| `NanumSquareRoundR.ttf` — 나눔스퀘어라운드 Regular | Latin + Korean | SIL Open Font License 1.1 | [NAVER official font collection](https://hangeul.naver.com/font) |
| `MPLUSRounded1c-Regular.ttf` | Japanese (kana + kanji), rounded | SIL Open Font License 1.1 | [Google Fonts / M+ FONTS](https://github.com/google/fonts/tree/main/ofl/mplusrounded1c) |
| `ZCOOLKuaiLe-Regular.ttf` | Simplified Chinese, rounded | SIL Open Font License 1.1 | [Google Fonts / ZCOOL](https://github.com/google/fonts/tree/main/ofl/zcoolkuaile) |

The Japanese/Chinese source fonts above are retained but are not loaded by the
current editor or included in the release package. Their SIL OFL 1.1 notices are
available in the linked upstream directories.

## NanumSquareRound attribution and license

NanumSquareRound Regular is provided by NAVER and designed by Sandoll Communications.
Copyright (c) 2017 NAVER Corporation. All rights reserved.
It is distributed under SIL OFL 1.1; see [the full notice](NanumSquareRound-LICENSE.txt)
and [NAVER's published terms](https://help.naver.com/service/30016/contents/18088?lang=ko&osType=PC).

The unmodified Regular TTF comes from [the official bundle](https://hangeul.naver.com/hangeul_static/webfont/zips/nanum-all_new.zip).
It is 1,063,276 bytes and covers all 11,172 modern Hangul syllables.
SHA-256: `52862fdf05b55ba886ca21998222d9598ef84c975e68046b9a8d8f1af09228a0`.
Commercial use and bundling with software are permitted while preserving the
copyright notice and full license. The font must retain OFL licensing and cannot
be sold by itself; modified fonts must respect reserved font names.
