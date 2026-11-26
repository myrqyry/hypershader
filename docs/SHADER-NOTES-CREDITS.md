# Shader Information in HyperShader

The shader properties panel has a **Shader Information** section below
shader parameters and global overrides. This is *documentation about the
shader*, not a personal notepad, and every field is **read-only**.

## Notes

The **Notes** section displays a shader-provided description, and optional
technical or usage notes. Shader authors can put these in the file's header:

    // @description "Iridescent bubble renderer"
    // @notes "Uses thin-film interference and raymarching; GPU intensive."

The @usage tag and plain Notes: header are also recognized as technical
notes. When no description exists, the panel says so instead of inventing
one. Technical notes only appear when supplied by the shader.

## Credits

The separate **Credits** section displays header-supplied author,
contributors/copyright, original source link, and license. Recognized tags
include @author, @credits, @source, @license, and corresponding
plain-comment labels (Author:, Credits:, Upstream:, License:).
An unspecified credit is explicitly marked as unknown; an absent author,
URL, or license is not made up.

The actual shader headers are the source of truth; the parser does not retrieve anything from the internet.

**There is no editable personal notes field.** Previous plugin versions
temporarily exposed a Your Notes field stored as shader_notes_* in OBS
scene/source settings. This update stops presenting or writing those fields,
but deliberately **does not delete pre-existing values** from saved scenes.
Nothing writes notes into downloaded .effect files.

The information reader examines at most 96 header lines, stops at shader
code, and escapes HTML markup before showing data in OBS's Qt information
labels. Shader switches refresh the displayed information.

## Validation

    scripts/verify-shader-info.sh
    scripts/verify-shader-ui-boundaries.sh
    scripts/verify-filter-ui-obs.sh

The metadata test verifies parsed descriptions, author/credits separation,
license and source, optional technical notes, and rejection of misleading
credits embedded after code begins. The native libobs test confirms all UI
information fields are read-only, checks the correct header information
while switching real shader presets, and proves prior note values remain
present in scene/source settings.

The tests operate with actual libobs/OpenGL rather than a mocked control
panel. They do not constitute a manual Qt-theme screenshot test.
