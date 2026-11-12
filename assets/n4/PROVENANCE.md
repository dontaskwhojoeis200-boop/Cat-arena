# assets/n4 provenance

`skybox-env-gallery_nebula.png` — the sky box of THE ARSENAL (`q3-gallery`), six 512x512 RGBA faces
stacked top to bottom in the legacy layer order rt, bk, lf, ft, up, dn.

Source: the unmodified `legacy/index.html` (SHA-256 e3c914bc...3510393). The legacy has no image
files for this box: `Q3Tex.skyBoxes['env/gallery_nebula'](suffix, 512)` paints each face in
JavaScript at load and `uploadSkyBox` (line 4366) uploads the result unflipped. The file was written
by `tools/n4/legacy_skybox.cjs`, which calls that generator with the same arguments in headless
Chromium, checks that two calls return identical bytes, and stores the bytes without any
conversion. Hashes of the file, of the raw RGBA data and of each face are in
`skybox-env-gallery_nebula.json`. Re-create with:

    node tools/n4/legacy_skybox.cjs --map q3-gallery --out assets/n4/skybox-env-gallery_nebula.png

It is derived game content of this project, covered by the project's own terms like assets/n1.
