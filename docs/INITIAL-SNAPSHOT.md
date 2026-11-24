# Initial distribution scope

This standalone repository starts from the project-authored HyperShaderFX
native framework, with a reduced preset library and retained upstream notices.
It has no imported collection history or connection to a previous Git remote.

Included: native OBS source/filter/transition framework, original instruments,
reactive sprites, Sight interoperability, visual shader builder, core shader
library, native test infrastructure, and credited dependencies. The module name
and OBS IDs are unchanged to preserve compatibility.

Withheld: unreviewed third-party shader collections, reference-based effects,
the explicitly ported Datamosh, Droste, Gravity, Pixel Sort and Hologram
libraries, optional model binaries, historical review imagery and training
dataset. Do not restore these merely because they existed in a development
checkout. Establish provenance, exact source revisions and applicable license
terms before adding them.

See [runtime notices](../data/licenses/README.md). This conservative selection
does not constitute independent legal clearance of every line of code.


## CI and showcase qualification

The curated repository runs its normal OBS31 build matrix and separate
OBS33 compatibility workflow on main/master changes and pull requests.
Checkout is commit-pinned; same-ref runs are superseded rather than
accumulating. The shared README link checker is adapted from the
Aftermarket and Streaming Media CI pilots. The six showcase previews are
existing fixture renders already included in the pinned release inventory;
no historical imagery or extra model assets were imported.

Local verification: branch/SDK contract, README relative-link regression,
source-export fixtures, Linux installer fixtures, OBS33 install and image
ABI contract checks, and shader settings tests all passed. This was a
documentation and workflow update, not a new platform build or live
renderer qualification. Hosted workflow results remain pending.
