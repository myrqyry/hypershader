/**
 * hypershader Visual Builder - Node Catalog
 * Defines all available nodes organized by category
 * Auto-generated from stdlib analysis + built-in nodes
 */

const NODE_CATALOG = {
    categories: [
        // ============================================
        // INPUT NODES
        // ============================================
        {
            name: 'Input',
            icon: '📥',
            color: '#22c55e',
            nodes: [
                {
                    id: 'input_uv',
                    name: 'UV Coordinates',
                    category: 'Input',
                    outputs: [{ name: 'output', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'input_fragcoord',
                    name: 'Fragment Coord',
                    category: 'Input',
                    outputs: [{ name: 'output', type: 'float2', label: 'fragCoord' }]
                },
                {
                    id: 'input_time',
                    name: 'Time',
                    category: 'Input',
                    outputs: [{ name: 'output', type: 'float', label: 'hs_time' }]
                },
                {
                    id: 'input_resolution',
                    name: 'Resolution',
                    category: 'Input',
                    outputs: [{ name: 'output', type: 'float3', label: 'hs_resolution' }]
                },
                {
                    id: 'input_mouse',
                    name: 'Mouse',
                    category: 'Input',
                    outputs: [{ name: 'output', type: 'float4', label: 'hs_pointer' }]
                },
                {
                    id: 'input_texture',
                    name: 'Source Texture',
                    category: 'Input',
                    outputs: [{ name: 'output', type: 'float4', label: 'Color' }]
                },
                {
                    id: 'input_float',
                    name: 'Float',
                    category: 'Input',
                    inputs: [{ name: 'value', type: 'float', default: 0, min: -10, max: 10, label: 'Value' }],
                    outputs: [{ name: 'output', type: 'float', label: 'Out' }],
                    exposeAsUniform: true
                },
                {
                    id: 'input_float2',
                    name: 'Vector 2',
                    category: 'Input',
                    inputs: [
                        { name: 'x', type: 'float', default: 0, label: 'X' },
                        { name: 'y', type: 'float', default: 0, label: 'Y' }
                    ],
                    outputs: [{ name: 'output', type: 'float2', label: 'Out' }]
                },
                {
                    id: 'input_float3',
                    name: 'Vector 3',
                    category: 'Input',
                    inputs: [
                        { name: 'r', type: 'float', default: 0, label: 'R/X' },
                        { name: 'g', type: 'float', default: 0, label: 'G/Y' },
                        { name: 'b', type: 'float', default: 0, label: 'B/Z' }
                    ],
                    outputs: [{ name: 'output', type: 'float3', label: 'Out' }]
                },
                {
                    id: 'input_color',
                    name: 'Color',
                    category: 'Input',
                    inputs: [
                        { name: 'r', type: 'float', default: 1, min: 0, max: 1, label: 'R' },
                        { name: 'g', type: 'float', default: 1, min: 0, max: 1, label: 'G' },
                        { name: 'b', type: 'float', default: 1, min: 0, max: 1, label: 'B' },
                        { name: 'a', type: 'float', default: 1, min: 0, max: 1, label: 'A' }
                    ],
                    outputs: [{ name: 'output', type: 'float4', label: 'Color' }]
                }
            ]
        },

        // ============================================
        // MEDIAPIPE & AUDIO
        // ============================================
        {
            name: 'MediaPipe',
            icon: '🧠',
            color: '#8b5cf6',
            nodes: [
                {
                    id: 'mp_segmentation',
                    name: 'Segmentation Mask',
                    category: 'MediaPipe',
                    expression: 'segmentation_mask(${uv})',
                    include: 'stdlib/mediapipe.glsl',
                    inputs: [{ name: 'uv', type: 'float2', label: 'UV' }],
                    outputs: [{ name: 'result', type: 'float', label: 'Mask' }]
                },
                {
                    id: 'mp_landmark',
                    name: 'Face Landmark',
                    category: 'MediaPipe',
                    expression: 'landmark_pos(${id})',
                    include: 'stdlib/mediapipe.glsl',
                    inputs: [{ name: 'id', type: 'int', default: 1, min: 0, max: 467, label: 'ID' }],
                    outputs: [{ name: 'result', type: 'float4', label: 'Pos (XYZV)' }]
                },
                {
                    id: 'mp_audio_bass',
                    name: 'Audio Bass',
                    category: 'MediaPipe',
                    expression: 'bass',
                    include: 'stdlib/mediapipe.glsl',
                    outputs: [{ name: 'result', type: 'float', label: 'Bass' }]
                },
                {
                    id: 'mp_audio_mid',
                    name: 'Audio Mid',
                    category: 'MediaPipe',
                    expression: 'mid',
                    include: 'stdlib/mediapipe.glsl',
                    outputs: [{ name: 'result', type: 'float', label: 'Mid' }]
                },
                {
                    id: 'mp_audio_treble',
                    name: 'Audio Treble',
                    category: 'MediaPipe',
                    expression: 'treble',
                    include: 'stdlib/mediapipe.glsl',
                    outputs: [{ name: 'result', type: 'float', label: 'Treble' }]
                },
                {
                    id: 'mp_audio_volume',
                    name: 'Audio Volume',
                    category: 'MediaPipe',
                    expression: 'volume',
                    include: 'stdlib/mediapipe.glsl',
                    outputs: [{ name: 'result', type: 'float', label: 'Volume' }]
                },
                {
                    id: 'mp_entity',
                    name: 'Physics Entity',
                    category: 'MediaPipe',
                    expression: 'getEntity(${id})',
                    include: 'stdlib/mediapipe.glsl',
                    returnType: 'Entity',
                    inputs: [{ name: 'id', type: 'int', default: 0, min: 0, max: 99, label: 'Entity ID' }],
                    outputs: [
                        { name: 'pos', type: 'float2', label: 'Position', mapping: 'pos' },
                        { name: 'scale', type: 'float', label: 'Scale', mapping: 'scale' },
                        { name: 'type', type: 'int', label: 'Type', mapping: 'type' }
                    ]
                }
            ]
        },

        // ============================================
        // OUTPUT NODES
        // ============================================
        {
            name: 'Output',
            icon: '📤',
            color: '#ef4444',
            nodes: [
                {
                    id: 'output_color',
                    name: 'Final Output',
                    category: 'Output',
                    inputs: [{ name: 'color', type: 'float4', label: 'Color' }]
                }
            ]
        },

        // ============================================
        // NOISE (from noise.glsl)
        // ============================================
        {
            name: 'Noise',
            icon: '🌊',
            color: '#3b82f6',
            nodes: [
                {
                    id: 'noise_hash11',
                    name: 'Hash 1D→1D',
                    category: 'Noise',
                    function: 'hash11',
                    include: 'noise.glsl',
                    inputs: [{ name: 'p', type: 'float', default: 0, label: 'Input' }],
                    outputs: [{ name: 'result', type: 'float', label: 'Hash' }]
                },
                {
                    id: 'noise_hash12',
                    name: 'Hash 2D→1D',
                    category: 'Noise',
                    function: 'hash12',
                    include: 'noise.glsl',
                    inputs: [{ name: 'p', type: 'float2', label: 'Position' }],
                    outputs: [{ name: 'result', type: 'float', label: 'Hash' }]
                },
                {
                    id: 'noise_value',
                    name: 'Value Noise',
                    category: 'Noise',
                    function: 'noise',
                    include: 'noise.glsl',
                    inputs: [{ name: 'p', type: 'float2', label: 'Position' }],
                    outputs: [{ name: 'result', type: 'float', label: 'Noise' }]
                },
                {
                    id: 'noise_fbm',
                    name: 'FBM',
                    category: 'Noise',
                    function: 'fbm',
                    include: 'noise.glsl',
                    inputs: [
                        { name: 'p', type: 'float2', label: 'Position' },
                        { name: 'octaves', type: 'int', default: 4, min: 1, max: 8, label: 'Octaves' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'FBM' }]
                }
            ]
        },

        // ============================================
        // COLORS (from colors.glsl)
        // ============================================
        {
            name: 'Colors',
            icon: '🎨',
            color: '#f59e0b',
            nodes: [
                {
                    id: 'color_rgb2hsv',
                    name: 'RGB → HSV',
                    category: 'Colors',
                    function: 'rgb2hsv',
                    include: 'colors.glsl',
                    inputs: [{ name: 'c', type: 'float3', label: 'RGB' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'HSV' }]
                },
                {
                    id: 'color_hsv2rgb',
                    name: 'HSV → RGB',
                    category: 'Colors',
                    function: 'hsv2rgb',
                    include: 'colors.glsl',
                    inputs: [{ name: 'c', type: 'float3', label: 'HSV' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'RGB' }]
                },
                {
                    id: 'color_grayscale',
                    name: 'Grayscale',
                    category: 'Colors',
                    function: 'grayscale',
                    include: 'colors.glsl',
                    inputs: [{ name: 'color', type: 'float3', label: 'Color' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'Gray' }]
                },
                {
                    id: 'color_invert',
                    name: 'Invert',
                    category: 'Colors',
                    function: 'invert',
                    include: 'colors.glsl',
                    inputs: [{ name: 'color', type: 'float3', label: 'Color' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'Inverted' }]
                },
                {
                    id: 'color_posterize',
                    name: 'Posterize',
                    category: 'Colors',
                    function: 'posterize',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'color', type: 'float3', label: 'Color' },
                        { name: 'levels', type: 'float', default: 4, min: 2, max: 32, label: 'Levels' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Posterized' }]
                },
                {
                    id: 'color_sepia',
                    name: 'Sepia',
                    category: 'Colors',
                    function: 'sepia',
                    include: 'colors.glsl',
                    inputs: [{ name: 'color', type: 'float3', label: 'Color' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'Sepia' }]
                },
                {
                    id: 'color_rainbow',
                    name: 'Rainbow',
                    category: 'Colors',
                    function: 'rainbow',
                    include: 'colors.glsl',
                    inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'Color' }]
                },
                {
                    id: 'color_heatmap',
                    name: 'Heatmap',
                    category: 'Colors',
                    function: 'heatmap',
                    include: 'colors.glsl',
                    inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'Color' }]
                },
                {
                    id: 'colormap_jet',
                    name: 'Jet (Scientific)',
                    category: 'Colors',
                    function: 'colormapJet',
                    include: 'colormaps.glsl',
                    inputs: [{ name: 't', type: 'float', default: 0.5, min: 0, max: 1, label: 'Value' }],
                    outputs: [{ name: 'result', type: 'float4', label: 'Color' }]
                },
                {
                    id: 'colormap_spectral',
                    name: 'Spectral',
                    category: 'Colors',
                    function: 'colormapSpectral',
                    include: 'colormaps.glsl',
                    inputs: [{ name: 't', type: 'float', default: 0.5, min: 0, max: 1, label: 'Value' }],
                    outputs: [{ name: 'result', type: 'float4', label: 'Color' }]
                },
                {
                    id: 'colormap_lava_waves',
                    name: 'Lava Waves',
                    category: 'Colors',
                    function: 'colormapLavaWaves',
                    include: 'colormaps.glsl',
                    inputs: [{ name: 't', type: 'float', default: 0.5, min: 0, max: 1, label: 'Value' }],
                    outputs: [{ name: 'result', type: 'float4', label: 'Color' }]
                },
                {
                    id: 'colormap_space',
                    name: 'Space',
                    category: 'Colors',
                    function: 'colormapSpace',
                    include: 'colormaps.glsl',
                    inputs: [{ name: 't', type: 'float', default: 0.5, min: 0, max: 1, label: 'Value' }],
                    outputs: [{ name: 'result', type: 'float4', label: 'Color' }]
                },
                {
                    id: 'color_palette',
                    name: 'IQ Palette',
                    category: 'Colors',
                    function: 'palette',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 't', type: 'float', default: 0, label: 'T' },
                        { name: 'a', type: 'float3', default: { x: 0.5, y: 0.5, z: 0.5 }, label: 'A' },
                        { name: 'b', type: 'float3', default: { x: 0.5, y: 0.5, z: 0.5 }, label: 'B' },
                        { name: 'c', type: 'float3', default: { x: 1, y: 1, z: 1 }, label: 'C' },
                        { name: 'd', type: 'float3', default: { x: 0, y: 0.33, z: 0.67 }, label: 'D' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Color' }]
                }
            ]
        },

        // ============================================
        // BLEND MODES (from colors.glsl)
        // ============================================
        {
            name: 'Blend',
            icon: '🔀',
            color: '#f97316',
            nodes: [
                {
                    id: 'blend_screen',
                    name: 'Screen',
                    category: 'Blend',
                    function: 'blendScreen',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'base', type: 'float3', label: 'Base' },
                        { name: 'blend', type: 'float3', label: 'Blend' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Result' }]
                },
                {
                    id: 'blend_multiply',
                    name: 'Multiply',
                    category: 'Blend',
                    function: 'blendMultiply',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'base', type: 'float3', label: 'Base' },
                        { name: 'blend', type: 'float3', label: 'Blend' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Result' }]
                },
                {
                    id: 'blend_overlay',
                    name: 'Overlay',
                    category: 'Blend',
                    function: 'blendOverlay',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'base', type: 'float3', label: 'Base' },
                        { name: 'blend', type: 'float3', label: 'Blend' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Result' }]
                },
                {
                    id: 'blend_softlight',
                    name: 'Soft Light',
                    category: 'Blend',
                    function: 'blendSoftLight',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'base', type: 'float3', label: 'Base' },
                        { name: 'blend', type: 'float3', label: 'Blend' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Result' }]
                },
                {
                    id: 'blend_difference',
                    name: 'Difference',
                    category: 'Blend',
                    function: 'blendDifference',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'base', type: 'float3', label: 'Base' },
                        { name: 'blend', type: 'float3', label: 'Blend' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Result' }]
                },
                {
                    id: 'blend_opacity',
                    name: 'Mix (Lerp)',
                    category: 'Blend',
                    function: 'blendWithOpacity',
                    include: 'colors.glsl',
                    inputs: [
                        { name: 'base', type: 'float3', label: 'A' },
                        { name: 'blend', type: 'float3', label: 'B' },
                        { name: 'opacity', type: 'float', default: 0.5, min: 0, max: 1, label: 'Mix' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Result' }]
                }
            ]
        },

        // ============================================
        // DISTORTION (from distortion.glsl)
        // ============================================
        {
            name: 'Distortion',
            icon: '🔮',
            color: '#8b5cf6',
            nodes: [
                {
                    id: 'distort_swirl',
                    name: 'Swirl',
                    category: 'Distortion',
                    function: 'uvSwirl',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'center', type: 'float2', default: { x: 0.5, y: 0.5 }, label: 'Center' },
                        { name: 'angle', type: 'float', default: 3.14, min: -10, max: 10, label: 'Angle' },
                        { name: 'radius', type: 'float', default: 0.5, min: 0, max: 2, label: 'Radius' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_bulge',
                    name: 'Bulge/Pinch',
                    category: 'Distortion',
                    function: 'uvBulgePinch',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'center', type: 'float2', default: { x: 0.5, y: 0.5 }, label: 'Center' },
                        { name: 'radius', type: 'float', default: 0.5, min: 0, max: 2, label: 'Radius' },
                        { name: 'magnitude', type: 'float', default: 0.5, min: -2, max: 2, label: 'Amount' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_wave',
                    name: 'Wave',
                    category: 'Distortion',
                    function: 'uvWave',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'frequency', type: 'float', default: 10, min: 0, max: 50, label: 'Frequency' },
                        { name: 'amplitude', type: 'float', default: 0.02, min: 0, max: 0.2, label: 'Amplitude' },
                        { name: 'phase', type: 'float', default: 0, label: 'Phase' },
                        { name: 'horizontal', type: 'bool', default: true, label: 'Horizontal' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_ripple',
                    name: 'Ripple',
                    category: 'Distortion',
                    function: 'uvRipple',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'center', type: 'float2', default: { x: 0.5, y: 0.5 }, label: 'Center' },
                        { name: 'frequency', type: 'float', default: 20, min: 0, max: 100, label: 'Frequency' },
                        { name: 'amplitude', type: 'float', default: 0.02, min: 0, max: 0.1, label: 'Amplitude' },
                        { name: 'phase', type: 'float', default: 0, label: 'Phase' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_kaleidoscope',
                    name: 'Kaleidoscope',
                    category: 'Distortion',
                    function: 'uvKaleidoscope',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'center', type: 'float2', default: { x: 0.5, y: 0.5 }, label: 'Center' },
                        { name: 'segments', type: 'int', default: 6, min: 2, max: 16, label: 'Segments' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_tile',
                    name: 'Tile/Mirror',
                    category: 'Distortion',
                    function: 'uvTile',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'tiles', type: 'float2', default: { x: 2, y: 2 }, label: 'Tiles' },
                        { name: 'mirror', type: 'bool', default: false, label: 'Mirror' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_pixelate',
                    name: 'Pixelate',
                    category: 'Distortion',
                    function: 'uvPixelate',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'resolution', type: 'float2', default: { x: 1920, y: 1080 }, label: 'Resolution' },
                        { name: 'pixelSize', type: 'float', default: 4, min: 1, max: 64, label: 'Pixel Size' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'distort_crt',
                    name: 'CRT Curve',
                    category: 'Distortion',
                    function: 'uvCRTCurve',
                    include: 'distortion.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'curvature', type: 'float', default: 0.1, min: 0, max: 0.5, label: 'Curvature' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                }
            ]
        },

        // ============================================
        // GLITCH (from glitch.glsl)
        // ============================================
        {
            name: 'Glitch',
            icon: '⚡',
            color: '#ec4899',
            nodes: [
                {
                    id: 'glitch_scanline',
                    name: 'Scanline Glitch',
                    category: 'Glitch',
                    function: 'uvScanlineGlitch',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'time', type: 'float', default: 0, label: 'Time' },
                        { name: 'intensity', type: 'float', default: 0.1, min: 0, max: 1, label: 'Intensity' },
                        { name: 'lineHeight', type: 'float', default: 0.01, min: 0.001, max: 0.1, label: 'Line Height' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'glitch_block',
                    name: 'Block Glitch',
                    category: 'Glitch',
                    function: 'uvBlockGlitch',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'time', type: 'float', default: 0, label: 'Time' },
                        { name: 'blockSize', type: 'float2', default: { x: 0.1, y: 0.05 }, label: 'Block Size' },
                        { name: 'intensity', type: 'float', default: 0.1, min: 0, max: 0.5, label: 'Intensity' },
                        { name: 'seed', type: 'float', default: 0, label: 'Seed' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'glitch_static',
                    name: 'Static Noise',
                    category: 'Glitch',
                    function: 'staticNoise',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'time', type: 'float', default: 0, label: 'Time' },
                        { name: 'intensity', type: 'float', default: 0.1, min: 0, max: 1, label: 'Intensity' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Noise' }]
                },
                {
                    id: 'glitch_quantize',
                    name: 'Quantize',
                    category: 'Glitch',
                    function: 'quantizeColor',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'color', type: 'float3', label: 'Color' },
                        { name: 'levels', type: 'float', default: 8, min: 2, max: 256, label: 'Levels' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Quantized' }]
                },
                {
                    id: 'glitch_bitcrush',
                    name: 'Bit Crush',
                    category: 'Glitch',
                    function: 'bitCrush',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'color', type: 'float3', label: 'Color' },
                        { name: 'bits', type: 'float', default: 4, min: 1, max: 8, label: 'Bits' }
                    ],
                    outputs: [{ name: 'result', type: 'float3', label: 'Crushed' }]
                },
                {
                    id: 'glitch_jitter',
                    name: 'Jitter',
                    category: 'Glitch',
                    function: 'uvJitter',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'time', type: 'float', default: 0, label: 'Time' },
                        { name: 'intensity', type: 'float', default: 0.01, min: 0, max: 0.1, label: 'Intensity' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'glitch_vhs',
                    name: 'VHS Warp',
                    category: 'Glitch',
                    function: 'uvVHSWarp',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'uv', type: 'float2', label: 'UV' },
                        { name: 'time', type: 'float', default: 0, label: 'Time' }
                    ],
                    outputs: [{ name: 'result', type: 'float2', label: 'UV' }]
                },
                {
                    id: 'glitch_hologram',
                    name: 'Hologram Flicker',
                    category: 'Glitch',
                    function: 'hologramFlicker',
                    include: 'glitch.glsl',
                    inputs: [
                        { name: 'time', type: 'float', default: 0, label: 'Time' },
                        { name: 'speed', type: 'float', default: 10, min: 1, max: 50, label: 'Speed' },
                        { name: 'minBrightness', type: 'float', default: 0.5, min: 0, max: 1, label: 'Min Brightness' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Brightness' }]
                }
            ]
        },

        // ============================================
        // MATH (utilities)
        // ============================================
        {
            name: 'Math',
            icon: '🔢',
            color: '#64748b',
            nodes: [
                {
                    id: 'math_add',
                    name: 'Add',
                    category: 'Math',
                    expression: '${a} + ${b}',
                    inputs: [
                        { name: 'a', type: 'float', default: 0, label: 'A' },
                        { name: 'b', type: 'float', default: 0, label: 'B' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Sum' }]
                },
                {
                    id: 'math_subtract',
                    name: 'Subtract',
                    category: 'Math',
                    expression: '${a} - ${b}',
                    inputs: [
                        { name: 'a', type: 'float', default: 0, label: 'A' },
                        { name: 'b', type: 'float', default: 0, label: 'B' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Difference' }]
                },
                {
                    id: 'math_multiply',
                    name: 'Multiply',
                    category: 'Math',
                    expression: '${a} * ${b}',
                    inputs: [
                        { name: 'a', type: 'float', default: 0, label: 'A' },
                        { name: 'b', type: 'float', default: 1, label: 'B' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Product' }]
                },
                {
                    id: 'math_divide',
                    name: 'Divide',
                    category: 'Math',
                    expression: '${a} / max(${b}, 0.0001)',
                    inputs: [
                        { name: 'a', type: 'float', default: 0, label: 'A' },
                        { name: 'b', type: 'float', default: 1, label: 'B' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Quotient' }]
                },
                {
                    id: 'math_sin',
                    name: 'Sine',
                    category: 'Math',
                    expression: 'sin(${x})',
                    inputs: [{ name: 'x', type: 'float', default: 0, label: 'X' }],
                    outputs: [{ name: 'result', type: 'float', label: 'sin(X)' }]
                },
                {
                    id: 'math_cos',
                    name: 'Cosine',
                    category: 'Math',
                    expression: 'cos(${x})',
                    inputs: [{ name: 'x', type: 'float', default: 0, label: 'X' }],
                    outputs: [{ name: 'result', type: 'float', label: 'cos(X)' }]
                },
                {
                    id: 'math_frac',
                    name: 'Frac',
                    category: 'Math',
                    expression: 'frac(${x})',
                    inputs: [{ name: 'x', type: 'float', default: 0, label: 'X' }],
                    outputs: [{ name: 'result', type: 'float', label: 'frac(X)' }]
                },
                {
                    id: 'math_clamp',
                    name: 'Clamp',
                    category: 'Math',
                    expression: 'clamp(${x}, ${min}, ${max})',
                    inputs: [
                        { name: 'x', type: 'float', default: 0, label: 'X' },
                        { name: 'min', type: 'float', default: 0, label: 'Min' },
                        { name: 'max', type: 'float', default: 1, label: 'Max' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Clamped' }]
                },
                {
                    id: 'math_lerp',
                    name: 'Lerp',
                    category: 'Math',
                    expression: 'lerp(${a}, ${b}, ${t})',
                    inputs: [
                        { name: 'a', type: 'float', default: 0, label: 'A' },
                        { name: 'b', type: 'float', default: 1, label: 'B' },
                        { name: 't', type: 'float', default: 0.5, min: 0, max: 1, label: 'T' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Lerped' }]
                },
                {
                    id: 'math_smoothstep',
                    name: 'Smoothstep',
                    category: 'Math',
                    expression: 'smoothstep(${edge0}, ${edge1}, ${x})',
                    inputs: [
                        { name: 'edge0', type: 'float', default: 0, label: 'Edge 0' },
                        { name: 'edge1', type: 'float', default: 1, label: 'Edge 1' },
                        { name: 'x', type: 'float', default: 0.5, label: 'X' }
                    ],
                    outputs: [{ name: 'result', type: 'float', label: 'Smooth' }]
                },
                {
                    id: 'math_length',
                    name: 'Length',
                    category: 'Math',
                    expression: 'length(${v})',
                    inputs: [{ name: 'v', type: 'float2', label: 'Vector' }],
                    outputs: [{ name: 'result', type: 'float', label: 'Length' }]
                },
                {
                    id: 'math_normalize',
                    name: 'Normalize',
                    category: 'Math',
                    expression: 'normalize(${v})',
                    inputs: [{ name: 'v', type: 'float2', label: 'Vector' }],
                    outputs: [{ name: 'result', type: 'float2', label: 'Normalized' }]
                }
            ]
        },

        // ============================================
        // TEXTURE (sampling)
        // ============================================
        {
            name: 'Texture',
            icon: '🖼️',
            color: '#84cc16',
            nodes: [
                {
                    id: 'tex_sample',
                    name: 'Sample Texture',
                    category: 'Texture',
                    expression: 'image.Sample(textureSampler, ${uv})',
                    inputs: [{ name: 'uv', type: 'float2', label: 'UV' }],
                    outputs: [{ name: 'result', type: 'float4', label: 'Color' }]
                },
                {
                    id: 'tex_split_rgb',
                    name: 'Split RGB',
                    category: 'Texture',
                    expression: '${color}.rgb',
                    inputs: [{ name: 'color', type: 'float4', label: 'Color' }],
                    outputs: [{ name: 'result', type: 'float3', label: 'RGB' }]
                },
                {
                    id: 'tex_split_alpha',
                    name: 'Get Alpha',
                    category: 'Texture',
                    expression: '${color}.a',
                    inputs: [{ name: 'color', type: 'float4', label: 'Color' }],
                    outputs: [{ name: 'result', type: 'float', label: 'Alpha' }]
                },
                {
                    id: 'tex_combine',
                    name: 'Combine RGBA',
                    category: 'Texture',
                    expression: 'float4(${rgb}, ${a})',
                    inputs: [
                        { name: 'rgb', type: 'float3', label: 'RGB' },
                        { name: 'a', type: 'float', default: 1, label: 'Alpha' }
                    ],
                    outputs: [{ name: 'result', type: 'float4', label: 'Color' }]
                }
            ]
        }
    ]
};

// Catalog helper class
class NodeCatalog {
    constructor(data) {
        this.data = data;
        this.nodeMap = new Map();
        
        // Build lookup map
        for (const category of data.categories) {
            for (const node of category.nodes) {
                this.nodeMap.set(node.id, { ...node, categoryName: category.name, categoryColor: category.color });
            }
        }
    }

    getCategories() {
        return this.data.categories;
    }

    findNode(nodeId) {
        return this.nodeMap.get(nodeId);
    }

    searchNodes(query) {
        const results = [];
        const q = query.toLowerCase();
        
        for (const [id, node] of this.nodeMap) {
            if (node.name.toLowerCase().includes(q) || id.toLowerCase().includes(q)) {
                results.push(node);
            }
        }
        
        return results;
    }
}

// Export
window.NODE_CATALOG = NODE_CATALOG;
window.NodeCatalog = NodeCatalog;
