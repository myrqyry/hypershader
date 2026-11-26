# Readable shader names in OBS and Shader Manager

HyperShader uses one shared display-name resolver for the OBS **Shader Preset**
dropdown, user presets, and the Qt **Shader Manager** catalog. These names are
presentation only; **stored paths, existing preset IDs, scenes, and shaders do
not change**.

Examples:

| Stored file | Display |
|---|---|
| root/pframe_drift.effect | Filter / P-Frame Drift |

## Titles

An effect's authored top-level header takes precedence:

    // @label "Ripple Glass"

The resolver only reads a bounded number of lines in the effect's own header;
it never expands shader includes or invokes the compiler to calculate names.
If there is no valid top-level title, it humanizes the file stem by replacing
separators and preserving common graphics and creator acronyms (3D, RGB, CRT,
SIREN, LostJared, NoiseMaker, etc.).

Parameter directives are not effect titles:

    // @label "Clown Nose" @default 1.0 @min 0.0

Those annotations remain associated with the parameter. Missing or unreadable
files fall back to the readable filename instead of failing selection.

## Roles and compatibility

Existing source/filter/transition classification still determines the role
prefix, including the reviewed procedural generators that are sources. A
collection appears as a separate readable segment.
Classifying a shader as Source in the picker does not change the underlying
OBS source type or its execution lane.

This is **not a shader rename**. Files continue to load from precisely the
same paths, and all four historical preset IDs remain unchanged. Other IDs
continue to be derived from paths, not their display names. The tested picker
also rejects duplicate names so two effects cannot silently appear to be the
same item.

## Two linked OBS dropdowns: Category then Shader

The native properties panel now shows **Category** directly above
**Shader Preset**, replacing a single very long shader list. OBS's normal
Qt property combobox does not provide real cascading submenus; these two
linked controls are supported through libobs's public property API.

The first dropdown is a navigation-only view:

| Category | Shader choices |
|---|---|
| Sources | All effects explicitly classified as sources |
| Filters | Filter-classified effects |
| Transitions | Transition-classified effects (when available) |
| My Presets | User-saved custom shader presets (when available) |
| All Shaders | Every installed shader, with role prefixes |

Additional collection folders create their own categories automatically.
An empty category isn't offered (e.g. Transitions before installing a
dedicated transition shader). The dropdown initially selects the relevant
role for a new HyperShader Filter/Source; if that role is unavailable,
it shows All Shaders. Existing category choices are stored per OBS source.

The second dropdown lists just the selected category's shaders, using
short, readable labels within each category. It retains Choose a shader...
at the top and Custom File... at the bottom. Selecting an effect
updates the same stored shader path and ID as before.

**Browsing categories never swaps or stops the shader already running.**
When the current shader isn't part of the new category, it's retained
as a Current / ... option so the selected preset is still shown.
Choosing a different shader is the only action that changes the effect.
Switching back to a previously chosen category restores its contents.

Category filtering is for finding effects, **not a compatibility rule**.
A shader remains selectable in another HyperShader plugin type through
All Shaders or its collection. Roles identify intended use, not guaranteed
output behavior. As the plugin has not been distributed, no separate
external scene-migration layer was introduced; its existing shader paths
and preset IDs remain unchanged to avoid needless disruptions locally.

## Validation

    scripts/verify-shader-categories.sh
    scripts/verify-category-picker.sh
    scripts/verify-filter-ui-obs.sh
    scripts/verify-shader-ui-boundaries.sh
    scripts/verify-shader-manager-catalog.sh
    scripts/verify-shader-library-browse.sh

The category unit tests check roles, collections, readable titles and
navigation labels. The native libobs test exercises all three OBS source
registrations, the actual category change callback, filtering, shader
selection, source-path resolution, current-effect pinning, saved category
on reopen, and all-shader fallback.

The existing native picker suite switches to All Shaders to validate that
every installed .effect still appears with its unchanged path-derived ID,
plus historical saved scene selection, controls and visibility. The Qt
shader manager uses the same display labels.
