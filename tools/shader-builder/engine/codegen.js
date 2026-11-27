/**
 * hypershader Visual Builder - Code Generator
 * Generates OBS-compatible .effect files from node graph
 */

class ShaderCodeGenerator {
    constructor() {
        this.varCounter = 0;
    }

    generate(graph) {
        if (!graph.getOutputNode()) return this.generateEmptyShader();
        const {includes, uniforms, codeLines, outputNode} = this.generateParts(graph);
        return this.buildEffectFile(includes, uniforms, codeLines, outputNode);
    }

    generateParts(graph, uniformPrefix = '') {
        this.varCounter = 0;
        this.nodeVariables = new Map();
        
        const outputNode = graph.getOutputNode();
        if (!outputNode) throw new Error('Every persistent pass requires a Final Output node');

        // Get sorted nodes
        const sortedNodeIds = graph.topologicalSort();
        
        // Collect includes and generate code
        const includes = new Set();
        const codeLines = [];
        const uniforms = new Map();

        for (const nodeId of sortedNodeIds) {
            const node = graph.getNode(nodeId);
            if (!node) continue;

            // Collect includes
            if (node.definition.include) {
                includes.add(node.definition.include);
            }

            // Collect uniforms for exposed parameters
            if (node.definition.exposeAsUniform) {
                for (const input of node.definition.inputs || []) {
                    const uniformName = "u_" + uniformPrefix + node.id + "_" + input.name;
                    uniforms.set(uniformName, {
                        type: input.type,
                        default: node.values[input.name] ?? input.default,
                        label: input.label || input.name,
                        min: input.min,
                        max: input.max
                    });
                }
            }

            // Generate code for this node
            const code = this.generateNodeCode(node, graph);
            if (code) {
                codeLines.push(this.traceNodeCode(node, code));
            }
        }

        return {includes, uniforms, codeLines, outputNode};
    }

    static validPassId(id) {
        return typeof id === 'string' && /^[a-z][a-z0-9_]{0,23}$/.test(id) &&
            !/_(order|previous|resolution|divisor|format)$/.test(id);
    }

    generatePersistent(passes, finalGraph) {
        if (!Array.isArray(passes) || passes.length < 1 || passes.length > 8)
            throw new Error('Persistent graphs require one to eight passes');
        const ids = new Set();
        for (const pass of passes) {
            if (!ShaderCodeGenerator.validPassId(pass.id) || ids.has(pass.id))
                throw new Error('Invalid or duplicate pass name: ' + pass.id);
            if (![1, 2, 4, 8].includes(pass.divisor))
                throw new Error('Invalid pass divisor: ' + pass.id);
            if (![0, 1].includes(pass.format))
                throw new Error('Invalid pass format: ' + pass.id);
            ids.add(pass.id);
        }
        const includes = new Set();
        const uniforms = new Map();
        const declarations = ['uniform int hs_graph_frame;', 'uniform float3 hs_graph_output_resolution;'];
        const functions = [];
        this.persistentPassIds = ids;
        try {
            for (const [index, pass] of passes.entries()) {
                const resource = 'hs_pass_' + pass.id;
                declarations.push('uniform int ' + resource + '_order = ' + index + ';',
                    'uniform int ' + resource + '_divisor = ' + pass.divisor + ';',
                    'uniform int ' + resource + '_format = ' + pass.format + ';',
                    'uniform texture2d ' + resource + ';',
                    'uniform texture2d ' + resource + '_previous;',
                    'uniform float3 ' + resource + '_resolution;');
            }
            for (const stage of [...passes, {id: null, graph: finalGraph}]) {
                const label = stage.id || 'Draw';
                const graph = stage.graph;
                if (!graph.getOutputNode() || !graph.getInputConnection(graph.getOutputNode().id, 'color'))
                    throw new Error('Connect the Final Output in ' + label);
                const parts = this.generateParts(graph, stage.id ? 'pass_' + stage.id + '_' : 'draw_');
                for (const include of parts.includes) includes.add(include);
                for (const [name, u] of parts.uniforms) uniforms.set(name, u);
                const fn = stage.id ? 'hs_stage_' + stage.id : 'hs_shade_pixel';
                const technique = stage.id ? 'Pass_' + stage.id : 'Draw';
                functions.push('// Render stage: ' + label,
                    'float4 ' + fn + '(VertData v_in) : TARGET {',
                    '    float4 finalColor = float4(0.0, 0.0, 0.0, 1.0);',
                    ...parts.codeLines, '    return finalColor;', '}',
                    'technique ' + technique + ' { pass { vertex_shader = VSDefault(v_in); pixel_shader = ' + fn + '(v_in); } }', '');
            }
            return '// Native persistent graph generated by hypershader Visual Builder\n' +
                '#include "../stdlib/hypershader_standard.glsl"\n' +
                this.includeSource(includes) + '\n' + this.uniformSource(uniforms) + '\n' +
                declarations.join('\n') + '\n\n' + functions.join('\n');
        } finally {
            this.persistentPassIds = null;
        }
    }

    // Keep a source breadcrumb for both native OBS compilation and browser
    // preview diagnostics. IDs/types come from the graph and node catalog;
    // sanitizing also keeps imported graph labels from injecting comments.
    traceNodeCode(node, code) {
        const id = String(node.id).replace(/[^a-zA-Z0-9_-]/g, '_');
        const type = String(node.type).replace(/[^a-zA-Z0-9_-]/g, '_');
        return `    // @node ${id} ${type}\n${code}\n    // @endnode`;
    }

    generateNodeCode(node, graph) {
        const def = node.definition;
        
        // Handle built-in nodes
        if (def.category === 'Input') {
            return this.generateInputNode(node, def, graph);
        }
        
        if (def.category === 'Output') {
            return this.generateOutputNode(node, graph);
        }

        // Handle function call nodes
        if (def.function) {
            return this.generateFunctionCall(node, graph, def);
        }

        // Handle custom/expression nodes
        if (def.expression) {
            return this.generateExpression(node, graph, def);
        }

        return null;
    }

    generateInputNode(node, def, graph) {
        const varName = this.getNodeVariable(node.id, 'output');
        
        switch (node.type) {
            case 'input_uv':
                return `    float2 ${varName} = v_in.uv;`;
            case 'input_fragcoord':
                return `    float2 ${varName} = v_in.uv * hs_resolution.xy;`;
            case 'input_time':
                return `    float ${varName} = hs_time;`;
            case 'input_resolution':
                return `    float3 ${varName} = hs_resolution;`;
            case 'input_mouse':
                return `    float4 ${varName} = hs_pointer;`;
            case 'input_texture':
                return `    float4 ${varName} = image.Sample(textureSampler, v_in.uv);`;
            case 'input_graph_frame':
                if (!this.persistentPassIds) throw new Error('Graph Frame requires a persistent graph');
                return '    float ' + varName + ' = float(hs_graph_frame);';
            case 'input_pass_current':
            case 'input_pass_previous':
            case 'input_pass_resolution': {
                const id = node.values.passId;
                if (!this.persistentPassIds) throw new Error('Pass input requires a persistent graph');
                if (!this.persistentPassIds.has(id)) throw new Error('Unknown persistent pass: ' + (id || '(choose a pass)'));
                const resource = 'hs_pass_' + id;
                if (node.type === 'input_pass_resolution')
                    return '    float3 ' + varName + ' = ' + resource + '_resolution;';
                const edge = graph.getInputConnection(node.id, 'uv');
                const uv = edge ? this.convertType(this.getNodeVariable(edge.from.nodeId, edge.from.port), edge, graph, 'float2') : 'v_in.uv';
                return '    float4 ' + varName + ' = ' + resource +
                    (node.type === 'input_pass_previous' ? '_previous' : '') + '.Sample(textureSampler, ' + uv + ');';
            }
            case 'input_float':
                const floatVal = node.values.value ?? 0;
                return `    float ${varName} = ${this.formatNumber(floatVal)};`;
            case 'input_float2':
                const x = node.values.x ?? 0;
                const y = node.values.y ?? 0;
                return `    float2 ${varName} = float2(${this.formatNumber(x)}, ${this.formatNumber(y)});`;
            case 'input_float3':
                const r = node.values.r ?? 0;
                const g = node.values.g ?? 0;
                const b = node.values.b ?? 0;
                return `    float3 ${varName} = float3(${this.formatNumber(r)}, ${this.formatNumber(g)}, ${this.formatNumber(b)});`;
            case 'input_color':
                const cr = node.values.r ?? 1;
                const cg = node.values.g ?? 1;
                const cb = node.values.b ?? 1;
                const ca = node.values.a ?? 1;
                return `    float4 ${varName} = float4(${this.formatNumber(cr)}, ${this.formatNumber(cg)}, ${this.formatNumber(cb)}, ${this.formatNumber(ca)});`;
            default:
                return `    // Unknown input: ${node.type}`;
        }
    }

    generateOutputNode(node, graph) {
        const edge = graph.getInputConnection(node.id, 'color');
        if (edge) {
            const inputVar = this.getNodeVariable(edge.from.nodeId, edge.from.port);
            const inputNode = graph.getNode(edge.from.nodeId);
            const inputType = inputNode?.definition.outputs?.find(o => o.name === edge.from.port)?.type || 'float4';
            
            // Convert to float4 if needed
            if (inputType === 'float3') {
                return `    finalColor = float4(${inputVar}, 1.0);`;
            } else if (inputType === 'float') {
                return `    finalColor = float4(${inputVar}, ${inputVar}, ${inputVar}, 1.0);`;
            } else if (inputType === 'float2') {
                return `    finalColor = float4(${inputVar}, 0.0, 1.0);`;
            }
            return `    finalColor = ${inputVar};`;
        }
        return `    finalColor = float4(0.0, 0.0, 0.0, 1.0); // No input connected`;
    }

    generateFunctionCall(node, graph, def) {
        const args = [];
        
        // Build arguments from inputs
        for (const input of def.inputs || []) {
            const edge = graph.getInputConnection(node.id, input.name);
            if (edge) {
                // Connected - use connected variable
                const connectedVar = this.getNodeVariable(edge.from.nodeId, edge.from.port);
                args.push(this.convertType(connectedVar, edge, graph, input.type));
            } else {
                // Not connected - use inline value
                const value = node.values[input.name] ?? input.default ?? 0;
                args.push(this.formatValue(value, input.type));
            }
        }

        // Generate output variables
        const outputs = def.outputs || [{ name: 'result', type: 'float' }];
        const lines = [];
        
        if (outputs.length === 1) {
            const varName = this.getNodeVariable(node.id, outputs[0].name);
            lines.push(`    ${outputs[0].type} ${varName} = ${def.function}(${args.join(', ')});`);
        } else {
            // Multi-output functions (rare, handle specially)
            for (const output of outputs) {
                const varName = this.getNodeVariable(node.id, output.name);
                lines.push(`    ${output.type} ${varName} = ${def.function}(${args.join(', ')}).${output.name};`);
            }
        }

        return lines.join('\n');
    }

    generateExpression(node, graph, def) {
        let expr = def.expression;
        
        // Replace input placeholders
        for (const input of def.inputs || []) {
            const edge = graph.getInputConnection(node.id, input.name);
            let value;
            if (edge) {
                value = this.getNodeVariable(edge.from.nodeId, edge.from.port);
                value = this.convertType(value, edge, graph, input.type);
            } else {
                value = this.formatValue(node.values[input.name] ?? input.default ?? 0, input.type);
            }
            expr = expr.replace(new RegExp(`\\$\\{${input.name}\\}`, 'g'), value);
        }

        const outputs = def.outputs || [{ name: 'result', type: 'float' }];
        const lines = [];

        if (outputs.length === 1) {
            const varName = this.getNodeVariable(node.id, outputs[0].name);
            lines.push(`    ${outputs[0].type} ${varName} = ${expr};`);
        } else {
            // Multi-output expression (e.g. returns a struct like Entity)
            const baseVar = `tmp_${node.id}`;
            const firstOutputType = def.returnType || 'Entity'; // Default to Entity for getEntity
            lines.push(`    ${firstOutputType} ${baseVar} = ${expr};`);
            
            for (const output of outputs) {
                const varName = this.getNodeVariable(node.id, output.name);
                const mapping = output.mapping || output.name;
                lines.push(`    ${output.type} ${varName} = ${baseVar}.${mapping};`);
            }
        }
        
        return lines.join('\n');
    }

    getNodeVariable(nodeId, portName) {
        const key = `${nodeId}_${portName}`;
        if (!this.nodeVariables.has(key)) {
            this.nodeVariables.set(key, `v${this.varCounter++}`);
        }
        return this.nodeVariables.get(key);
    }

    convertType(varName, edge, graph, targetType) {
        const fromNode = graph.getNode(edge.from.nodeId);
        const fromPortDef = fromNode?.definition.outputs?.find(o => o.name === edge.from.port);
        const fromType = fromPortDef?.type || 'float';

        if (fromType === targetType) return varName;

        // Swizzle conversions
        if (fromType === 'float' && targetType === 'float2') {
            return `float2(${varName}, ${varName})`;
        }
        if (fromType === 'float' && targetType === 'float3') {
            return `float3(${varName}, ${varName}, ${varName})`;
        }
        if (fromType === 'float' && targetType === 'float4') {
            return `float4(${varName}, ${varName}, ${varName}, 1.0)`;
        }
        if (fromType === 'float2' && targetType === 'float') {
            return `${varName}.x`;
        }
        if (fromType === 'float3' && targetType === 'float') {
            return `${varName}.x`;
        }
        if (fromType === 'float4' && targetType === 'float') {
            return `${varName}.x`;
        }
        if (fromType === 'float4' && targetType === 'float3') {
            return `${varName}.rgb`;
        }
        if (fromType === 'float3' && targetType === 'float4') {
            return `float4(${varName}, 1.0)`;
        }
        if (fromType === 'float2' && targetType === 'float3') {
            return `float3(${varName}, 0.0)`;
        }

        return varName;
    }

    formatNumber(num) {
        const n = parseFloat(num);
        if (Number.isInteger(n)) {
            return n.toFixed(1);
        }
        return n.toString();
    }

    formatValue(value, type) {
        switch (type) {
            case 'int':
                return Math.floor(value).toString();
            case 'float':
                return this.formatNumber(value);
            case 'float2':
                if (typeof value === 'object') {
                    return `float2(${this.formatNumber(value.x || 0)}, ${this.formatNumber(value.y || 0)})`;
                }
                return `float2(${this.formatNumber(value)}, ${this.formatNumber(value)})`;
            case 'float3':
                if (typeof value === 'object') {
                    return `float3(${this.formatNumber(value.x || value.r || 0)}, ${this.formatNumber(value.y || value.g || 0)}, ${this.formatNumber(value.z || value.b || 0)})`;
                }
                return `float3(${this.formatNumber(value)}, ${this.formatNumber(value)}, ${this.formatNumber(value)})`;
            case 'float4':
                if (typeof value === 'object') {
                    return `float4(${this.formatNumber(value.x || value.r || 0)}, ${this.formatNumber(value.y || value.g || 0)}, ${this.formatNumber(value.z || value.b || 0)}, ${this.formatNumber(value.w || value.a || 1)})`;
                }
                return `float4(${this.formatNumber(value)}, ${this.formatNumber(value)}, ${this.formatNumber(value)}, 1.0)`;
            case 'bool':
                return value ? 'true' : 'false';
            default:
                return value.toString();
        }
    }

    /**
     * Generate an explicit WebGL2 approximation for the standalone builder.
     * Only known browser-compatible nodes are allowed; native-only nodes fail
     * visibly instead of silently returning a misleading preview.
     */
    buildPreviewShader(graph) {
        if (!graph.getOutputNode()) {
            throw new Error('Add a Final Output node to render a preview');
        }
        const functions = new Set([
            'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'abs', 'min', 'max',
            'clamp', 'lerp', 'mix', 'pow', 'sqrt', 'length', 'normalize',
            'dot', 'distance', 'frac', 'fract', 'floor', 'ceil', 'step',
            'smoothstep', 'mod', 'cross', 'exp', 'log'
        ]);
        this.varCounter = 0;
        this.nodeVariables = new Map();
        const lines = [];
        let needsSwirl = false;
        const previewFunctions = new Set();
        const colormapFunctions = new Set();
        // Ignore disconnected experiments in the node editor. Only nodes
        // feeding the final output belong to the rendered preview.
        const reachable = new Set();
        const visit = (id) => {
            if (reachable.has(id)) return;
            reachable.add(id);
            const node = graph.getNode(id);
            for (const input of node?.definition.inputs || []) {
                const edge = graph.getInputConnection(id, input.name);
                if (edge) visit(edge.from.nodeId);
            }
        };
        visit(graph.getOutputNode().id);
        for (const id of graph.topologicalSort()) {
            if (!reachable.has(id)) continue;
            const node = graph.getNode(id);
            if (!node) continue;
            const def = node.definition;
            if (def.include || (def.function && !functions.has(def.function))) {
                if (def.include === 'colormaps.glsl' &&
                    Object.prototype.hasOwnProperty.call(window.COLORMAP_KERNELS || {}, def.function)) {
                    colormapFunctions.add(def.function);
                } else if (node.type === 'distort_swirl' && def.function === 'uvSwirl') {
                    needsSwirl = true;
                } else if (['uvWave','uvRipple','uvCRTCurve','sepia','grayscale','invert'].includes(def.function)) {
                    previewFunctions.add(def.function);
                } else {
                    throw new Error(`Browser preview unsupported for native-only node: ${def.name || node.type}. Native OBS export is still available.`);
                }
            }
            const line = this.generateNodeCode(node, graph);
            if (line) lines.push(this.traceNodeCode(node, line));
        }
        const body = lines.join('\n')
            .replace(/\bfloat2\b/g, 'vec2')
            .replace(/\bfloat3\b/g, 'vec3')
            .replace(/\bfloat4\b/g, 'vec4')
            .replace(/\blerp\s*\(/g, 'mix(')
            .replace(/\bfrac\s*\(/g, 'fract(')
            .replace(/\bv_in\.uv\b/g, '(gl_FragCoord.xy / hs_resolution.xy)')
            .replace(/\bimage\.Sample\(textureSampler,\s*/g, 'texture(uSource, ');
        if (/\bVertData\b|\.Sample\s*\(|#include/.test(body)) {
            throw new Error('This native shader expression has no WebGL2 preview adapter');
        }
        const colormapSource = Array.from(colormapFunctions)
            .map(name => window.COLORMAP_KERNELS[name]).join('\n\n');
        const swirl = needsSwirl ? `
vec2 uvSwirl(vec2 uv, vec2 center, float angle, float radius) {
    vec2 delta = uv - center;
    float dist = length(delta);
    if (dist < radius && radius > 0.0) {
        float ratio = (radius - dist) / radius;
        float theta = ratio * ratio * angle;
        float s = sin(theta), c = cos(theta);
        delta = vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
    }
    return center + delta;
}
` : '';
        const previewAdapters = {
            uvWave: 'vec2 uvWave(vec2 uv, float freq, float amp, float phase, bool horizontal) { float off = sin((horizontal ? uv.y : uv.x) * freq + phase) * amp; return uv + (horizontal ? vec2(off,0.0) : vec2(0.0,off)); }',
            uvRipple: 'vec2 uvRipple(vec2 uv, vec2 centre, float freq, float amp, float phase) { vec2 d = uv-centre; float l = length(d); return l > 0.0001 ? uv + (d/l) * sin(l*freq-phase)*amp : uv; }',
            uvCRTCurve: 'vec2 uvCRTCurve(vec2 uv, float k) { vec2 p=uv*2.0-1.0; return p*(1.0+k*dot(p,p))*0.5+0.5; }',
            invert: 'vec3 invert(vec3 c) {return 1.0-c;}',
            grayscale: 'vec3 grayscale(vec3 c) {float y=dot(c,vec3(0.299,0.587,0.114)); return vec3(y);}',
            sepia: 'vec3 sepia(vec3 c) {return vec3(dot(c,vec3(0.393,0.769,0.189)),dot(c,vec3(0.349,0.686,0.168)),dot(c,vec3(0.272,0.534,0.131)));}'
        };
        const extraSource = [...previewFunctions].map(name => previewAdapters[name]).join('\n');
        return `#version 300 es
precision highp float;
uniform float hs_time;
uniform vec3 hs_resolution;
uniform vec4 hs_pointer;
uniform sampler2D uSource;
out vec4 previewColor;
${swirl}
${extraSource}
${colormapSource}
void main() {
    vec4 finalColor = vec4(0.0, 0.0, 0.0, 1.0);
${body}
    previewColor = finalColor;
}
`;
    }

    includeSource(includes) {
        return Array.from(includes).map(i => `#include "../stdlib/${i.replace(/^stdlib\//, '')}"`).join('\n');
    }

    uniformSource(uniforms) {
        return Array.from(uniforms.entries()).map(([name, u]) => {
            let line = `uniform ${u.type} ${name}`;
            const meta = [];
            if (u.default !== undefined) meta.push(`@default ${u.default}`);
            if (u.min !== undefined) meta.push(`@min ${u.min}`);
            if (u.max !== undefined) meta.push(`@max ${u.max}`);
            if (u.label) meta.push(`@label "${u.label}"`);
            if (meta.length > 0) {
                line += `; // ${meta.join(' ')}`;
            } else {
                line += ';';
            }
            return line;
        }).join('\n');
    }

    buildEffectFile(includes, uniforms, codeLines, outputNode) {
        const includeLines = this.includeSource(includes);
        const uniformLines = this.uniformSource(uniforms);

        const mainBody = codeLines.join('\n');

        return `// ============================================
// Auto-generated by hypershader Visual Builder
// https://github.com/myrqyry/hypershader
// ============================================

// Standard includes
#include "../stdlib/hypershader_standard.glsl"
${includeLines}

// Custom uniforms
${uniformLines || '// (none)'}

// Shader technique
float4 hs_shade_pixel(VertData v_in) : TARGET {
    float4 finalColor = float4(0.0, 0.0, 0.0, 1.0);
    
${mainBody}
    
    return finalColor;
}

technique Draw {
    pass {
        vertex_shader = VSDefault(v_in);
        pixel_shader = hs_shade_pixel(v_in);
    }
}
`;
    }

    generateEmptyShader() {
        return `// ============================================
// Auto-generated by hypershader Visual Builder
// Add nodes and connect them to generate shader code
// ============================================

#include "../stdlib/hypershader_standard.glsl"

float4 hs_shade_pixel(VertData v_in) : TARGET {
    // Sample source
    float4 color = image.Sample(textureSampler, v_in.uv);
    return color;
}

technique Draw {
    pass {
        vertex_shader = VSDefault(v_in);
        pixel_shader = hs_shade_pixel(v_in);
    }
}
`;
    }
}

// Export
window.ShaderCodeGenerator = ShaderCodeGenerator;
