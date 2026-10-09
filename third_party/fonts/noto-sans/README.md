# Noto Sans, Latin subset

The font of the launcher and the install's progress window (docs/launcher.md, stage 1, step 4),
embedded in the executable (`src/platform/CMakeLists.txt`). SIL Open Font License 1.1
(`OFL.txt`, no Reserved Font Name; THIRD_PARTY_NOTICES.md).

- Source: `NotoSans-v2.015.zip` from the release `NotoSans-v2.015` of
  https://github.com/notofonts/latin-greek-cyrillic (SHA-256
  `0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5`), its
  `NotoSans/unhinted/ttf/NotoSans-Regular.ttf` (SHA-256
  `f3961a9cde016d41a4879aecda1474d3a36d6bf54fa0e4643de029cc2248b0e8`) and `OFL.txt`.
- `NotoSans-Regular-Latin.ttf` (SHA-256
  `96f57335c350b98e684b02a0f892f4eb2d1904e9a15f89269c97727ff9f5cc70`, 27 008 bytes): Basic Latin,
  Latin-1 Supplement and Latin Extended-A (the letters of de, fr, es and more), general
  punctuation (dashes, quotes, ellipsis, guillemets), the euro and trade mark signs; made with
  fontTools 4.55.3:

  ```sh
  pyftsubset NotoSans-Regular.ttf \
    --unicodes="U+0020-007E,U+00A0-017F,U+2010-2027,U+2030-203A,U+20AC,U+2122,U+FFFD" \
    --layout-features="kern" --no-hinting --output-file=NotoSans-Regular-Latin.ttf
  ```

`launcher_window_test` checks that every character of `data/ui/tl_setup_strings.txt` is in it.
