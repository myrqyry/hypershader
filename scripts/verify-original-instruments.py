#!/usr/bin/env python3
"""Check the bounded first-party instrument set before native OBS GPU probing.

The native GPU probe is scripts/verify-original-instruments-obs.cpp.
"""
from pathlib import Path
import re
from hashlib import sha256

root = Path(__file__).resolve().parents[1]
expected = {
    'source.brass_register.effect': ('audio_band0', 'audio_band7', 'audio_kick'),
    'source.paper_tide.effect': ('audio_envelope', 'audio_band2', 'audio_beat_phase'),
    'source.spectral_garden.effect': ('audio_band0', 'audio_band7', 'audio_kick'),
    'filter.topographic_etch.effect': ('iSightDepthPresent', 'iSightDepth', 'audio_beat'),
    'filter.contact_filaments.effect': ('iSightHandCount', 'iSightHands', 'iSightPinch0'),
    'filter.afterimage_press.effect': ('iPreviousFrame', 'image', 'audio_flux'),
}
for filename, uniforms in expected.items():
    file = root / 'data/shaders/root' / filename
    assert file.is_file(), f'Missing original instrument: {filename}'
    text = file.read_text()
    assert '@label ' in text and '@description ' in text, filename
    assert 'technique Draw' in text or 'HYPERSHADER_MAINIMAGE_WRAPPER' in text
    for uniform in uniforms:
        assert re.search(r'\b' + re.escape(uniform) + r'\b', text), (filename, uniform)
    digest = sha256(file.read_bytes()).hexdigest()[:16]
    preview = file.parent / 'previews' / f'{file.stem}-{digest}.png'
    assert preview.is_file(), f'Missing source-hashed GPU preview: {filename}'
    # Avoid any direct network/media dependencies or preexisting third-party imports.
    assert not re.search(r'http[s]?://|#include\s+"\.\./\.\./(?!stdlib)', text), filename
print('ORIGINAL_INSTRUMENT_CONTRACT_PASS=6')
