/**
 * HyperShader - AI Pilot
 * Integrates FunctionGemma for NL-to-Graph and Graph Modification
 */

class ShaderAIPilot {
    constructor(app) {
        this.app = app;
        this.isGenerating = false;
    }

    /**
     * Send user prompt to Gemma to generate or modify graph
     */
    async processPrompt(prompt) {
        if (this.isGenerating) return;
        this.isGenerating = true;

        const currentGraph = this.app.graph.serialize();
        const catalogData = this.getSimplifiedCatalog();

        const systemPrompt = `You are the HyperShader AI Pilot. 
Your goal is to help the user build or modify an OBS shader node graph.
You MUST output ONLY valid JSON that represents the updated graph.

AVAILABLE NODES:
${JSON.stringify(catalogData, null, 2)}

CURRENT GRAPH:
${JSON.stringify(currentGraph, null, 2)}

INSTRUCTION:
If the user asks for a new effect, replace the CURRENT GRAPH.
If the user asks to modify (e.g., "make it pulse", "add blur"), modify the CURRENT GRAPH.
Maintain logical connections. Position nodes in a clear left-to-right flow.
Output format: { "version": 1, "nodes": [...], "edges": [...] }`;

        try {
            this.showToast('🧠 Gemma is thinking...', 'info');
            
            // Call C++ backend via bridge
            // We expect the bridge to expose 'window.hyperShader_generateText(prompt)'
            if (typeof window.hyperShader_generateText !== 'function') {
                throw new Error("AI Backend not available (Bridge missing)");
            }

            const fullPrompt = `${systemPrompt}\n\nUSER REQUEST: ${prompt}\n\nUPDATED JSON GRAPH:`;
            const result = await window.hyperShader_generateText(fullPrompt);

            if (!result) throw new Error("Gemma returned empty result");

            // Extract JSON from result (Gemma might wrap it in markdown blocks)
            const jsonStr = this.extractJSON(result);
            const newGraphData = JSON.parse(jsonStr);

            if (newGraphData && newGraphData.nodes) {
                this.app.graph.deserialize(newGraphData, this.app.catalog);
                this.app.updateCode();
                this.app.fitView();
                this.showToast('✨ Graph updated by AI', 'success');
            }

        } catch (err) {
            console.error("AI Pilot Error:", err);
            this.showToast(`AI Error: ${err.message}`, 'error');
        } finally {
            this.isGenerating = false;
        }
    }

    getSimplifiedCatalog() {
        return this.app.catalog.getCategories().map(cat => ({
            category: cat.name,
            nodes: cat.nodes.map(n => ({
                id: n.id,
                name: n.name,
                inputs: n.inputs?.map(i => ({ name: i.name, type: i.type, label: i.label })),
                outputs: n.outputs?.map(o => ({ name: o.name, type: o.type, label: o.label }))
            }))
        }));
    }

    extractJSON(str) {
        const start = str.indexOf('{');
        const end = str.lastIndexOf('}');
        if (start === -1 || end === -1) return str;
        return str.substring(start, end + 1);
    }

    showToast(msg, type) {
        if (this.app.showToast) this.app.showToast(msg, type);
        else alert(msg);
    }
}

window.ShaderAIPilot = ShaderAIPilot;
