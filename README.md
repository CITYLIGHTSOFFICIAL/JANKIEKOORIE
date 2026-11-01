# KOOR – één stem → een heel koor

VST3-plugin (en AU op Mac) die van één zangstem direct een koor maakt.

## Hoe het werkt
Elke koorstem is een eigen pitch-shifter met een eigen:
- **ontstemming** (een paar cent hoger of lager),
- **timing** (iets achter de leadzang, zoals echte zangers),
- **vibrato** en langzame toonhoogte-drift,
- **klankkleur** en **positie in het stereobeeld**.

Een ingebouwde toonhoogtedetector laat de shifters synchroon lopen met jouw stem, zodat het koor vol klinkt en niet "flangerig". Daarnaast zijn er extra stemgroepen een octaaf lager en hoger, en een galm.

## Knoppen
| Knop | Wat het doet |
|---|---|
| Stemmen | 1–16 koorstemmen |
| Ontstemming | hoeveel de stemmen van elkaar afwijken in toonhoogte (cent) |
| Timing | hoeveel de stemmen achter elkaar aan zingen (ms) |
| Vibrato | hoeveel vibrato elke stem heeft |
| Breedte | stereobreedte van het koor |
| Helderheid | donkere ↔ heldere klank van de stemmen |
| Octaaf −1 / +1 | voegt een basgroep / sopraangroep toe |
| Galm | hal-galm op het koor |
| Mix | verhouding originele stem ↔ koor |
| Uitvoer | eindvolume |

Presets: Klein ensemble, Koor, Groot koor, Gospel, Engelen, Monniken, Muur van stemmen.

Tip: zet KOOR op een droge, goed gestemde leadvocal (eventueel na autotune) — hoe schoner de input, hoe mooier het koor.

## De plugin bouwen (geen programmeerkennis nodig)
Makkelijkste manier, via GitHub (gratis):
1. Maak een nieuwe repository op github.com en upload de inhoud van deze map (inclusief de map `.github`).
2. Ga naar het tabblad **Actions**. De build start vanzelf (± 10 minuten).
3. Klik op de afgeronde build en download onderaan **KOOR-Windows** of **KOOR-macOS**.
4. Pak het uit en zet `KOOR.vst3` in je plugin-map:
   - Windows: `C:\Program Files\Common Files\VST3\`
   - Mac: `/Library/Audio/Plug-Ins/VST3/` (AU: `/Library/Audio/Plug-Ins/Components/`)
5. Herstart je DAW en laat hem opnieuw naar plugins scannen.

Op Mac kan de eerste keer een beveiligingsmelding komen omdat de plugin niet ondertekend is. Oplossing in Terminal:
`xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/KOOR.vst3`

Zelf bouwen op je computer: installeer CMake en Visual Studio (Windows) of Xcode (Mac), dan:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Licentie
Gebouwd met JUCE 8. Voor persoonlijk gebruik en kleine makers (< $50k omzet per jaar) is de gratis JUCE Starter-licentie voldoende; ga je de plugin verkopen, check dan juce.com/get-juce.
