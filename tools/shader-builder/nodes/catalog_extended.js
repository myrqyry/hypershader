/**
 * HyperShaderFX Visual Builder - Extended Node Catalog
 * Additional stdlib modules: Animation, Blur, Edges, Grading, Oscillators, 
 * Transitions, Retro, Mapping, Lighting
 */

// Extend the main catalog with additional categories
const EXTENDED_CATEGORIES = [
    // ============================================
    // ANIMATION (from animation.glsl)
    // ============================================
    {
        name: 'Animation',
        icon: '🎬',
        color: '#a855f7',
        nodes: [
            { id: 'anim_ease_in_quad', name: 'Ease In Quad', category: 'Animation', function: 'easeInQuad', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_out_quad', name: 'Ease Out Quad', category: 'Animation', function: 'easeOutQuad', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_in_out_quad', name: 'Ease InOut Quad', category: 'Animation', function: 'easeInOutQuad', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_in_cubic', name: 'Ease In Cubic', category: 'Animation', function: 'easeInCubic', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_out_cubic', name: 'Ease Out Cubic', category: 'Animation', function: 'easeOutCubic', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_out_elastic', name: 'Ease Out Elastic', category: 'Animation', function: 'easeOutElastic', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_out_bounce', name: 'Ease Out Bounce', category: 'Animation', function: 'easeOutBounce', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_ease_out_back', name: 'Ease Out Back', category: 'Animation', function: 'easeOutBack', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Eased' }] },
            { id: 'anim_pingpong', name: 'Ping Pong', category: 'Animation', function: 'pingPong', include: 'animation.glsl',
              inputs: [{ name: 'time', type: 'float', default: 0, label: 'Time' }, { name: 'period', type: 'float', default: 1, min: 0.1, max: 10, label: 'Period' }],
              outputs: [{ name: 'result', type: 'float', label: 'Value' }] },
            { id: 'anim_triangle_wave', name: 'Triangle Wave', category: 'Animation', function: 'triangleWave', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'anim_sawtooth', name: 'Sawtooth Wave', category: 'Animation', function: 'sawtoothWave', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'anim_square_wave', name: 'Square Wave', category: 'Animation', function: 'squareWave', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }, { name: 'dutyCycle', type: 'float', default: 0.5, min: 0, max: 1, label: 'Duty' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'anim_stepped', name: 'Stepped', category: 'Animation', function: 'stepped', include: 'animation.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }, { name: 'steps', type: 'float', default: 4, min: 2, max: 16, label: 'Steps' }],
              outputs: [{ name: 'result', type: 'float', label: 'Value' }] }
        ]
    },

    // ============================================
    // BLUR (from blur.glsl)
    // ============================================
    {
        name: 'Blur',
        icon: '💨',
        color: '#60a5fa',
        nodes: [
            { id: 'blur_gaussian1d', name: 'Gaussian 1D', category: 'Blur', function: 'gaussian1D', include: 'blur.glsl',
              inputs: [{ name: 'x', type: 'float', default: 0, label: 'X' }, { name: 'sigma', type: 'float', default: 1, min: 0.1, max: 5, label: 'Sigma' }],
              outputs: [{ name: 'result', type: 'float', label: 'Weight' }] },
            { id: 'blur_kawase', name: 'Kawase Blur', category: 'Blur', function: 'kawaseBlur', include: 'blur.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' },
                       { name: 'offset', type: 'float', default: 1, min: 0, max: 4, label: 'Offset' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'blur_radial', name: 'Radial Blur', category: 'Blur', function: 'radialBlur', include: 'blur.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'center', type: 'float2', default: {x:0.5,y:0.5}, label: 'Center' },
                       { name: 'strength', type: 'float', default: 0.1, min: 0, max: 0.5, label: 'Strength' },
                       { name: 'samples', type: 'int', default: 16, min: 4, max: 32, label: 'Samples' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'blur_directional', name: 'Directional Blur', category: 'Blur', function: 'directionalBlur', include: 'blur.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' },
                       { name: 'angle', type: 'float', default: 0, min: 0, max: 6.28, label: 'Angle' },
                       { name: 'strength', type: 'float', default: 10, min: 0, max: 50, label: 'Strength' },
                       { name: 'samples', type: 'int', default: 16, min: 4, max: 32, label: 'Samples' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] }
        ]
    },

    // ============================================
    // EDGES (from edges.glsl)
    // ============================================
    {
        name: 'Edges',
        icon: '✏️',
        color: '#f472b6',
        nodes: [
            { id: 'edge_sobel', name: 'Sobel Edge', category: 'Edges', function: 'sobelEdge', include: 'edges.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' }],
              outputs: [{ name: 'result', type: 'float', label: 'Edge' }] },
            { id: 'edge_prewitt', name: 'Prewitt Edge', category: 'Edges', function: 'prewittEdge', include: 'edges.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' }],
              outputs: [{ name: 'result', type: 'float', label: 'Edge' }] },
            { id: 'edge_laplacian', name: 'Laplacian Edge', category: 'Edges', function: 'laplacianEdge', include: 'edges.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' }],
              outputs: [{ name: 'result', type: 'float', label: 'Edge' }] },
            { id: 'edge_sharpen', name: 'Sharpen', category: 'Edges', function: 'sharpen', include: 'edges.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' },
                       { name: 'strength', type: 'float', default: 1, min: 0, max: 5, label: 'Strength' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'edge_emboss', name: 'Emboss', category: 'Edges', function: 'emboss', include: 'edges.glsl',
              inputs: [{ name: 'tex', type: 'texture', label: 'Texture' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'texelSize', type: 'float2', default: {x:0.001,y:0.001}, label: 'Texel Size' },
                       { name: 'strength', type: 'float', default: 1, min: 0, max: 3, label: 'Strength' },
                       { name: 'angle', type: 'float', default: 0.785, min: 0, max: 6.28, label: 'Angle' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] }
        ]
    },

    // ============================================
    // GRADING (from grading.glsl)
    // ============================================
    {
        name: 'Grading',
        icon: '🎛️',
        color: '#fbbf24',
        nodes: [
            { id: 'grade_exposure', name: 'Exposure', category: 'Grading', function: 'adjustExposure', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'exposure', type: 'float', default: 0, min: -3, max: 3, label: 'Exposure' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'grade_contrast', name: 'Contrast', category: 'Grading', function: 'adjustContrast', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'contrast', type: 'float', default: 1, min: 0, max: 3, label: 'Contrast' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'grade_saturation', name: 'Saturation', category: 'Grading', function: 'adjustSaturation', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'saturation', type: 'float', default: 1, min: 0, max: 3, label: 'Saturation' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'grade_vibrance', name: 'Vibrance', category: 'Grading', function: 'adjustVibrance', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'vibrance', type: 'float', default: 0, min: -1, max: 1, label: 'Vibrance' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'grade_white_balance', name: 'White Balance', category: 'Grading', function: 'applyWhiteBalance', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' },
                       { name: 'temperature', type: 'float', default: 0, min: -1, max: 1, label: 'Temp' },
                       { name: 'tint', type: 'float', default: 0, min: -1, max: 1, label: 'Tint' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'grade_lgg', name: 'Lift/Gamma/Gain', category: 'Grading', function: 'applyLGG', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' },
                       { name: 'lift', type: 'float3', default: {x:0,y:0,z:0}, label: 'Lift' },
                       { name: 'gamma', type: 'float3', default: {x:1,y:1,z:1}, label: 'Gamma' },
                       { name: 'gain', type: 'float3', default: {x:1,y:1,z:1}, label: 'Gain' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'grade_split_tone', name: 'Split Tone', category: 'Grading', function: 'applySplitTone', include: 'grading.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' },
                       { name: 'shadowColor', type: 'float3', default: {x:1,y:0.9,z:0.8}, label: 'Shadow' },
                       { name: 'highlightColor', type: 'float3', default: {x:0.8,y:0.9,z:1}, label: 'Highlight' },
                       { name: 'balance', type: 'float', default: 0.5, min: 0, max: 1, label: 'Balance' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] }
        ]
    },

    // ============================================
    // OSCILLATORS (from oscillators.glsl)
    // ============================================
    {
        name: 'Oscillators',
        icon: '〰️',
        color: '#34d399',
        nodes: [
            { id: 'osc_sine', name: 'Sine Osc', category: 'Oscillators', function: 'osc_sine', include: 'oscillators.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'osc_square', name: 'Square Osc', category: 'Oscillators', function: 'osc_square', include: 'oscillators.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'osc_saw', name: 'Saw Osc', category: 'Oscillators', function: 'osc_saw', include: 'oscillators.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'osc_triangle', name: 'Triangle Osc', category: 'Oscillators', function: 'osc_triangle', include: 'oscillators.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'osc_pulse', name: 'Pulse Osc', category: 'Oscillators', function: 'osc_pulse', include: 'oscillators.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }, { name: 'width', type: 'float', default: 0.5, min: 0, max: 1, label: 'Width' }],
              outputs: [{ name: 'result', type: 'float', label: 'Wave' }] },
            { id: 'osc_noise_step', name: 'Noise S&H', category: 'Oscillators', function: 'osc_noise_step', include: 'oscillators.glsl',
              inputs: [{ name: 't', type: 'float', default: 0, label: 'T' }],
              outputs: [{ name: 'result', type: 'float', label: 'Random' }] }
        ]
    },

    // ============================================
    // TRANSITIONS (from transitions.glsl)
    // ============================================
    {
        name: 'Transitions',
        icon: '🔄',
        color: '#818cf8',
        nodes: [
            { id: 'trans_dissolve', name: 'Dissolve', category: 'Transitions', function: 'transitionDissolve', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'trans_luma_wipe', name: 'Luma Wipe', category: 'Transitions', function: 'transitionLumaWipe', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' },
                       { name: 'sharpness', type: 'float', default: 10, min: 1, max: 50, label: 'Sharp' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'trans_radial_wipe', name: 'Radial Wipe', category: 'Transitions', function: 'transitionRadialWipe', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 'uv', type: 'float2', label: 'UV' }, { name: 'center', type: 'float2', default: {x:0.5,y:0.5}, label: 'Center' },
                       { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' },
                       { name: 'softness', type: 'float', default: 0.1, min: 0, max: 0.5, label: 'Soft' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'trans_clock_wipe', name: 'Clock Wipe', category: 'Transitions', function: 'transitionClockWipe', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 'uv', type: 'float2', label: 'UV' }, { name: 'center', type: 'float2', default: {x:0.5,y:0.5}, label: 'Center' },
                       { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' },
                       { name: 'softness', type: 'float', default: 0.05, min: 0, max: 0.2, label: 'Soft' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'trans_diamond', name: 'Diamond Wipe', category: 'Transitions', function: 'transitionDiamond', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 'uv', type: 'float2', label: 'UV' }, { name: 'center', type: 'float2', default: {x:0.5,y:0.5}, label: 'Center' },
                       { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' },
                       { name: 'softness', type: 'float', default: 0.1, min: 0, max: 0.3, label: 'Soft' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'trans_blinds', name: 'Blinds', category: 'Transitions', function: 'transitionBlinds', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 'uv', type: 'float2', label: 'UV' }, { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' },
                       { name: 'numBlinds', type: 'float', default: 10, min: 2, max: 30, label: 'Blinds' },
                       { name: 'horizontal', type: 'bool', default: true, label: 'Horizontal' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] },
            { id: 'trans_checker', name: 'Checkerboard', category: 'Transitions', function: 'transitionCheckerboard', include: 'transitions.glsl',
              inputs: [{ name: 'colorA', type: 'float4', label: 'A' }, { name: 'colorB', type: 'float4', label: 'B' },
                       { name: 'uv', type: 'float2', label: 'UV' }, { name: 't', type: 'float', default: 0, min: 0, max: 1, label: 'T' },
                       { name: 'gridSize', type: 'float', default: 8, min: 2, max: 20, label: 'Grid' }],
              outputs: [{ name: 'result', type: 'float4', label: 'Color' }] }
        ]
    },

    // ============================================
    // RETRO (from retro.glsl)
    // ============================================
    {
        name: 'Retro',
        icon: '📺',
        color: '#fb923c',
        nodes: [
            { id: 'retro_scanlines', name: 'Scanlines', category: 'Retro', function: 'retro_scanlines', include: 'retro.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'uv', type: 'float2', label: 'UV' },
                       { name: 'count', type: 'float', default: 480, min: 100, max: 1080, label: 'Count' },
                       { name: 'opacity', type: 'float', default: 0.3, min: 0, max: 1, label: 'Opacity' },
                       { name: 'width', type: 'float', default: 1, min: 0.5, max: 2, label: 'Width' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'retro_curve', name: 'CRT Curve', category: 'Retro', function: 'retro_curve', include: 'retro.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'strength', type: 'float', default: 0.1, min: 0, max: 0.3, label: 'Curve' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'retro_aperture', name: 'Aperture Grille', category: 'Retro', function: 'retro_maskAperture', include: 'retro.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'fragCoord', type: 'float2', label: 'fragCoord' },
                       { name: 'str', type: 'float', default: 0.3, min: 0, max: 1, label: 'Strength' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'retro_shadow_mask', name: 'Shadow Mask', category: 'Retro', function: 'retro_maskShadow', include: 'retro.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'fragCoord', type: 'float2', label: 'fragCoord' },
                       { name: 'str', type: 'float', default: 0.3, min: 0, max: 1, label: 'Strength' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'retro_dither_bayer', name: 'Bayer Dither', category: 'Retro', function: 'retro_ditherBayer', include: 'retro.glsl',
              inputs: [{ name: 'pos', type: 'float2', label: 'Position' }],
              outputs: [{ name: 'result', type: 'float', label: 'Dither' }] },
            { id: 'retro_ps1_jitter', name: 'PS1 Jitter', category: 'Retro', function: 'ps1_jitter', include: 'retro.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'level', type: 'float', default: 256, min: 30, max: 400, label: 'Level' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'retro_ps1_color', name: 'PS1 Color Depth', category: 'Retro', function: 'ps1_colorDepth', include: 'retro.glsl',
              inputs: [{ name: 'color', type: 'float3', label: 'Color' }, { name: 'levels', type: 'float', default: 32, min: 4, max: 64, label: 'Levels' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Color' }] },
            { id: 'retro_vhs_tracking', name: 'VHS Tracking', category: 'Retro', function: 'vhs_tracking', include: 'retro.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'time', type: 'float', default: 0, label: 'Time' },
                       { name: 'intensity', type: 'float', default: 1, min: 0, max: 5, label: 'Intensity' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'retro_rgb2yiq', name: 'RGB to YIQ', category: 'Retro', function: 'rgb2yiq', include: 'retro.glsl',
              inputs: [{ name: 'c', type: 'float3', label: 'RGB' }],
              outputs: [{ name: 'result', type: 'float3', label: 'YIQ' }] },
            { id: 'retro_yiq2rgb', name: 'YIQ to RGB', category: 'Retro', function: 'yiq2rgb', include: 'retro.glsl',
              inputs: [{ name: 'c', type: 'float3', label: 'YIQ' }],
              outputs: [{ name: 'result', type: 'float3', label: 'RGB' }] }
        ]
    },

    // ============================================
    // MAPPING (from mapping.glsl)
    // ============================================
    {
        name: 'Mapping',
        icon: '🗺️',
        color: '#4ade80',
        nodes: [
            { id: 'map_rotate', name: 'UV Rotate', category: 'Mapping', function: 'uvRotate', include: 'mapping.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'angleDegrees', type: 'float', default: 0, min: -180, max: 180, label: 'Angle' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'map_scale', name: 'UV Scale', category: 'Mapping', function: 'uvScale', include: 'mapping.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'scale', type: 'float2', default: {x:1,y:1}, label: 'Scale' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'map_offset', name: 'UV Offset', category: 'Mapping', function: 'uvOffset', include: 'mapping.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'offset', type: 'float2', default: {x:0,y:0}, label: 'Offset' },
                       { name: 'scroll', type: 'float2', default: {x:0,y:0}, label: 'Scroll' }, { name: 'time', type: 'float', default: 0, label: 'Time' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'map_polar', name: 'To Polar', category: 'Mapping', function: 'cartesianToPolar', include: 'mapping.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'center', type: 'float2', default: {x:0.5,y:0.5}, label: 'Center' }],
              outputs: [{ name: 'result', type: 'float2', label: 'Polar' }] },
            { id: 'map_cartesian', name: 'To Cartesian', category: 'Mapping', function: 'polarToCartesian', include: 'mapping.glsl',
              inputs: [{ name: 'polar', type: 'float2', label: 'Polar' }, { name: 'center', type: 'float2', default: {x:0.5,y:0.5}, label: 'Center' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'map_flipbook', name: 'Flipbook UV', category: 'Mapping', function: 'flipbookUV', include: 'mapping.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }, { name: 'columns', type: 'float', default: 4, min: 1, max: 16, label: 'Cols' },
                       { name: 'rows', type: 'float', default: 4, min: 1, max: 16, label: 'Rows' },
                       { name: 'frame', type: 'float', default: 0, min: 0, max: 64, label: 'Frame' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] },
            { id: 'map_mirror', name: 'UV Mirror', category: 'Mapping', function: 'uvMirror', include: 'mapping.glsl',
              inputs: [{ name: 'uv', type: 'float2', label: 'UV' }],
              outputs: [{ name: 'result', type: 'float2', label: 'UV' }] }
        ]
    },

    // ============================================
    // SDF 3D (from sdf.glsl)
    // ============================================
    {
        name: 'SDF',
        icon: '📐',
        color: '#06b6d4',
        nodes: [
            { id: 'sdf_sphere', name: 'SD Sphere', category: 'SDF', function: 'sdSphere', include: 'sdf.glsl',
              inputs: [{ name: 'p', type: 'float3', label: 'Position' }, { name: 's', type: 'float', default: 1, min: 0, max: 5, label: 'Radius' }],
              outputs: [{ name: 'result', type: 'float', label: 'Distance' }] },
            { id: 'sdf_box', name: 'SD Box', category: 'SDF', function: 'sdBox', include: 'sdf.glsl',
              inputs: [{ name: 'p', type: 'float3', label: 'Position' }, { name: 'b', type: 'float3', default: {x:1,y:1,z:1}, label: 'Size' }],
              outputs: [{ name: 'result', type: 'float', label: 'Distance' }] },
            { id: 'sdf_torus', name: 'SD Torus', category: 'SDF', function: 'sdTorus', include: 'sdf.glsl',
              inputs: [{ name: 'p', type: 'float3', label: 'Position' }, { name: 't', type: 'float2', default: {x:1,y:0.3}, label: 'Radii' }],
              outputs: [{ name: 'result', type: 'float', label: 'Distance' }] },
            { id: 'sdf_capsule', name: 'SD Capsule', category: 'SDF', function: 'sdCapsule', include: 'sdf.glsl',
              inputs: [{ name: 'p', type: 'float3', label: 'Position' },
                       { name: 'a', type: 'float3', default: {x:0,y:-0.5,z:0}, label: 'Point A' },
                       { name: 'b', type: 'float3', default: {x:0,y:0.5,z:0}, label: 'Point B' },
                       { name: 'r', type: 'float', default: 0.2, min: 0, max: 1, label: 'Radius' }],
              outputs: [{ name: 'result', type: 'float', label: 'Distance' }] },
            { id: 'sdf_smooth_min', name: 'Smooth Min', category: 'SDF', function: 'smin', include: 'sdf.glsl',
              inputs: [{ name: 'a', type: 'float', default: 0, label: 'A' }, { name: 'b', type: 'float', default: 0, label: 'B' },
                       { name: 'k', type: 'float', default: 0.1, min: 0, max: 1, label: 'K' }],
              outputs: [{ name: 'result', type: 'float', label: 'Min' }] },
            { id: 'sdf_rotate_y', name: 'Rotate Y', category: 'SDF', function: 'opRotateY', include: 'sdf.glsl',
              inputs: [{ name: 'p', type: 'float3', label: 'Position' }, { name: 'a', type: 'float', default: 0, min: -6.28, max: 6.28, label: 'Angle' }],
              outputs: [{ name: 'result', type: 'float3', label: 'Position' }] }
        ]
    }
];

// Merge extended categories into main catalog
if (typeof NODE_CATALOG !== 'undefined') {
    NODE_CATALOG.categories = NODE_CATALOG.categories.concat(EXTENDED_CATEGORIES);
}

// Export for standalone use
window.EXTENDED_CATEGORIES = EXTENDED_CATEGORIES;
