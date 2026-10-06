# Languages and translations

The game ships English plus German, French and Spanish. The language is chosen in the game's own
menu (Options → Settings, the *Video* column on the right, *Language*); the change applies after a
restart. The choice is saved with the other host settings in
`~/.config/TorchlightRecomp/settings.toml`.

## Language packs

Any other language can be added as a language pack: a folder `translations/<code>/` in the extracted
game data (next to `translations/de`, `fr` and `es`) with:

- `translation.dat.adm`: the translation, in the game's format (pairs of English original and
  translation). The game looks texts up by their exact English original.
- optionally `media/UI/`: fonts for the language's script. A pack's files take the place of the
  game's with the same path. Scripts the game's fonts lack (Cyrillic, for instance) need `.font`
  files pointing to a font that has them, including `Serif14.font`, which the Xbox 360 version has
  and the PC version does not.

Packs appear in the *Language* list by their folder name. If the selected pack is removed, the game
starts in English.

## Translations from the PC version

Translations made for Torchlight on PC use the same `translation.dat.adm` format and can be used as
packs. The official Russian translation of the GOG release, for instance, covers about 92% of the
Xbox 360 texts, with its fonts from the release's `Pak.zip` (`media/UI/*.font` and `ariblk.ttf`). To
take it from your own GOG installer without running it:

```sh
innoextract --language=ru-RU -I translations -I Pak.zip -d /tmp/tl_ru setup_torchlight_*_(russian)_*.exe
```

What a PC translation lacks is the text that only exists on the Xbox 360 (Xbox LIVE messages,
leaderboards, the controller tutorials, a few menus such as *New Character*) and texts whose English
changed between versions. Mods made for the PC version's mods folder (for instance translations that
replace game data files) are not supported.

Scripts written right to left (Arabic) are not supported. Chinese and Japanese are not supported yet.

## Completing a translation

`tools/translations/tl_translate.py` lists what a pack is missing and adds what someone translated;
it works on the game data directly and never runs the game:

```sh
# Write translations/ru/missing.txt with every text the pack has no translation for.
tools/translations/tl_translate.py missing --game-data ~/360tools/extracted/extracted --lang ru

# After filling it in: add the translated entries to the pack
# (the previous translation.dat.adm is kept as translation.dat.adm.bak).
tools/translations/tl_translate.py merge --game-data ~/360tools/extracted/extracted --lang ru \
  ~/360tools/extracted/extracted/translations/ru/missing.txt
```

The file is UTF-8, one entry per pair of lines, taken exactly as written after the prefix:

```
EN: New Character
TR: Новый персонаж
```

Entries left empty are ignored, so a file can be filled in bit by bit and merged again; `missing`
then lists only what is still left. `\n` in a text is the game's line break. Markers such as
`[VALUE]`, `[ITEM]` or `|cFFFFBA00…|u` must stay in the translation (in any order); `merge` skips
entries whose markers differ from the original's and says which. Starting a new language works the
same way: with no pack yet, `missing` lists all of the game's texts.
