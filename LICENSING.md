# Licensing and provenance

FXP-derived files under `src/fxp`, `resources/FxpTriangles.hlsl` and `tests/fxp`
originate from FXP Optimized 0.2.2. Their MIT notice is preserved in
`licenses/FXP-Optimized-MIT.txt`. Integration modifications are marked by the
1.3.0 changelog and validation notes.

The pre-existing SanFrame/SAVO-derived project did not contain a project-wide
license. This cleanup does not invent one or relicense inherited code. A public
repository can retain these provenance notes; an open-source release license
for the remaining implementation still needs to be selected by its rights
holders. Dependency licenses do not license the whole project.

No redistribution license accompanied `resources/BankGothic-Bold.ttf`.
The file remains available locally but is ignored by Git and omitted from the
source package. Local builds embed it when available unless
`SANFRAME_EMBED_LOCAL_FONT=OFF` is set. The local release package therefore
contains the font within the ASI; source/CI builds without the file use the
scalable Dear ImGui fallback. Check the font's redistribution terms before
publishing a binary that embeds it.
