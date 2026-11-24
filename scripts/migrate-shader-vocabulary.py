#!/usr/bin/env python3
"""Opt-in migration of pre-namespace HyperShader effect source; not an engine importer."""
import argparse
import re
from pathlib import Path

NAMES = {
    "HYPERSHADER_MAINIMAGE_WRAPPER": "HYPERSHADER_PIXEL_WRAPPER",
    "SHADERTOY_MAINIMAGE_WRAPPER": "HYPERSHADER_PIXEL_WRAPPER",
    "iBufferA": "hs_buffer_a",
    "iBufferADivisor": "hs_buffer_a_divisor",
    "iBufferAFormat": "hs_buffer_a_format",
    "iBufferAPrevious": "hs_buffer_a_previous",
    "iBufferAResolution": "hs_buffer_a_resolution",
    "iBufferB": "hs_buffer_b",
    "iBufferBDivisor": "hs_buffer_b_divisor",
    "iBufferBFormat": "hs_buffer_b_format",
    "iBufferBPrevious": "hs_buffer_b_previous",
    "iBufferBResolution": "hs_buffer_b_resolution",
    "iChannel0": "hs_input0",
    "iChannel1": "hs_input1",
    "iChannel2": "hs_input2",
    "iChannel3": "hs_input3",
    "iChannelAtlas": "hs_input_atlas",
    "iChannelAtlas0": "hs_input_atlas0",
    "iChannelAtlas1": "hs_input_atlas1",
    "iChannelAtlas2": "hs_input_atlas2",
    "iChannelAtlas3": "hs_input_atlas3",
    "iChannelResolution": "hs_input_resolution",
    "iChannelResolution0": "hs_input_resolution0",
    "iChannelResolution1": "hs_input_resolution1",
    "iChannelResolution2": "hs_input_resolution2",
    "iChannelResolution3": "hs_input_resolution3",
    "iChannelTime": "hs_input_time",
    "iChannelTime0": "hs_input_time0",
    "iChannelTime1": "hs_input_time1",
    "iChannelTime2": "hs_input_time2",
    "iChannelTime3": "hs_input_time3",
    "iDate": "hs_date",
    "iFacePresent": "hs_face_present",
    "iFaceSurface": "hs_face_surface",
    "iFaceTriangles": "hs_face_triangles",
    "iFrame": "hs_frame_index",
    "iFrameRate": "hs_frame_rate",
    "iGraphBudgetMiB": "hs_graph_budget_mib",
    "iGraphFrame": "hs_graph_frame",
    "iGraphOutputResolution": "hs_graph_output_resolution",
    "iLandmarks": "hs_landmarks",
    "iMouse": "hs_pointer",
    "iPreviousFrame": "hs_history",
    "iProgress": "hs_transition_progress",
    "iReferenceLandmarks": "hs_reference_landmarks",
    "iResolution": "hs_resolution",
    "iSampleRate": "hs_sample_rate",
    "iScale": "hs_scale",
    "iSegmentation": "hs_segmentation",
    "iSightBlendshapes": "hs_sight_blendshapes",
    "iSightDepth": "hs_sight_depth",
    "iSightDepthPresent": "hs_sight_depth_present",
    "iSightFlow": "hs_sight_flow",
    "iSightFlowCoverage": "hs_sight_flow_coverage",
    "iSightFlowInterval": "hs_sight_flow_interval",
    "iSightFlowPresent": "hs_sight_flow_present",
    "iSightFlowStep": "hs_sight_flow_step",
    "iSightGesture0": "hs_sight_gesture0",
    "iSightGesture1": "hs_sight_gesture1",
    "iSightHandCount": "hs_sight_hand_count",
    "iSightHands": "hs_sight_hands",
    "iSightHeadRows": "hs_sight_head_rows",
    "iSightKind": "hs_sight_kind",
    "iSightMattePresent": "hs_sight_matte_present",
    "iSightMotionEnergy": "hs_sight_motion_energy",
    "iSightMotionPresent": "hs_sight_motion_present",
    "iSightPinch0": "hs_sight_pinch0",
    "iSightPinch1": "hs_sight_pinch1",
    "iSightPointCount": "hs_sight_point_count",
    "iSightPoints": "hs_sight_points",
    "iSightPresent": "hs_sight_present",
    "iSightRipple0": "hs_sight_ripple0",
    "iSightRipple1": "hs_sight_ripple1",
    "iSightRipple2": "hs_sight_ripple2",
    "iSightRipple3": "hs_sight_ripple3",
    "iSightRipple4": "hs_sight_ripple4",
    "iSightRipple5": "hs_sight_ripple5",
    "iSightRipple6": "hs_sight_ripple6",
    "iSightRipple7": "hs_sight_ripple7",
    "iSightSubjectBounds": "hs_sight_subject_bounds",
    "iSightSubjectCenter": "hs_sight_subject_center",
    "iSightSubjectCoverage": "hs_sight_subject_coverage",
    "iSightSubjectPresent": "hs_sight_subject_present",
    "iSightTextBounds": "hs_sight_text_bounds",
    "iSightTextCount": "hs_sight_text_count",
    "iSightTextMask": "hs_sight_text_mask",
    "iSightTextPresent": "hs_sight_text_present",
    "iSightTouch0": "hs_sight_touch0",
    "iSightTouch1": "hs_sight_touch1",
    "iTime": "hs_time",
    "iTimeDelta": "hs_delta_time",
    "mainImage": "hs_shade_pixel"
}

# Lex strings/comments atomically so only complete code identifiers change.
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?(?:\*/|\Z)|"(?:\\.|[^"\\])*"|\b[A-Za-z_][A-Za-z0-9_]*\b')


def convert(source):
    if re.search(r'^\s*#include\s+"[^"]*shadertoy_compat\.glsl"', source, re.M):
        raise ValueError("Explicit external adapter input: keep the adapter or convert it separately.")
    identifiers = {m.group() for m in TOKEN.finditer(source) if m.group()[0].isalpha() or m.group()[0] == "_"}
    for old, new in NAMES.items():
        if old in identifiers and new in identifiers:
            raise ValueError(f"Mixed old and native identifiers: {old} / {new}; review manually.")
    return TOKEN.sub(lambda m: NAMES.get(m.group(), m.group()), source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        with args.source.open(encoding="utf-8", newline="") as stream:
            source = stream.read()
        converted = convert(source)
        # Exclusive creation, even if input and output are the same file.
        with args.destination.open("x", encoding="utf-8", newline="") as stream:
            stream.write(converted)
    except (ValueError, OSError) as exc:
        parser.exit(1, f"{exc}\n")
    print(f"Migrated {args.source} -> {args.destination}; original unchanged.")


if __name__ == "__main__":
    main()
